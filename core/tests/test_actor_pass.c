#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/actor_pass.h"
/* Synthetic state for the cores. The image base is placed so that the
 * actor list globals (4a9af8h, 4a9afeh) and the RNG state address fall
 * inside the arena; objects live above them. The three helpers resolve to
 * the cores themselves, the other callees to small models: distances are
 * the larger axis delta, sight is a per-actor byte, the heading is 0, the
 * timers count down by 1, rng_bounded returns 7, V3c returns 0 and V44
 * accepts arguments below 4000h. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0xa9000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x40000u)
#define V3C UINT32_C(0x12340000)
#define V44 UINT32_C(0x12340004)
#define V48 UINT32_C(0x12340008)
static uint8_t arena[0x48000];static unsigned checks,calls[12],scans;static uint32_t scanned[8];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"actor_pass line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static es2_weapon_graph graph(void);
static uint32_t magnitude(uint32_t v){return (v&0x80000000u)?0u-v:v;}
static uint32_t distance(uint32_t p,uint32_t q){
    uint32_t x=magnitude(r32(0,p)-r32(0,q)),y=magnitude(r32(0,p+4)-r32(0,q+4));return x>y?x:y;
}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;es2_weapon_graph g=graph();uint64_t p=0;uint32_t entry[3]={r->eax,r->edx,r->ecx};
    if(f==VA(0x467a48)){CHECK(n==1);++calls[0];uint16_t t=r16(0,a[0]+1);if(t)--t;w16(0,a[0]+1,t);p=(a[0]&0xffff0000u)|t;}
    else if(f==VA(0x492f24)){CHECK(n==2&&a[0]==VA(0x4d2599)&&a[1]==1000);++calls[1];p=7;}
    else if(f==VA(0x4928d0)||f==VA(0x49288c)){CHECK(n==2);++calls[2];p=distance(a[0],a[1]);}
    else if(f==VA(0x412608)){CHECK(n==2);++calls[3];p=r8(a[0]+0x1f0);}         /* sight byte of the viewer */
    else if(f==VA(0x492934)){CHECK(n==2);++calls[4];p=0;}
    else if(f==VA(0x423430)){CHECK(n==1);++calls[5];}
    else if(f==V3C){CHECK(n==1);++calls[6];p=r16(0,a[0]+0x298);}
    else if(f==V44){CHECK(n==2);++calls[7];p=(uint16_t)a[1]<0x4000u;}
    else if(f==V48){CHECK(n==2);++calls[8];}
    else if(f==VA(0x41251c)){CHECK(n==1);++calls[9];p=es2_contact_expire_registers(&memory,&g,a[0],entry);}
    else if(f==VA(0x4128f8)){CHECK(n==1);++calls[10];if(scans<8)scanned[scans++]=a[0];p=es2_contact_scan_registers(&memory,&g,a[0],entry);}
    else if(f==VA(0x412704)){CHECK(n==2);++calls[11];p=es2_contact_share_registers(&memory,&g,a[0],a[1],entry);}
    else CHECK(0);
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);
}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,0,0,0,0,call,0,0,0};return g;}
/* Actors 0x40000 (stride 0x300), groups 0x43000 (0x7a), a vtable at
 * 0x44000, member arrays at 0x44100 and the list at 0x44200. */
static uint32_t actor(unsigned i){return OBJECTS+0x300u*i;}
static uint32_t group(unsigned i){return OBJECTS+0x3000u+0x7au*i;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void place(uint32_t a,int32_t x,int32_t y){w32(0,a+0x26,(uint32_t)x);w32(0,a+0x2a,(uint32_t)y);w32(0,a+0x2e,0);}
static int knows(uint32_t a,uint32_t b){return r8(a+0xc2+r16(0,b+0x4b))!=0;}
static void reset(void){
    memset(arena,0,sizeof(arena));memset(calls,0,sizeof(calls));scans=0;
    uint32_t vtable=OBJECTS+0x4000,members=OBJECTS+0x4100,list=OBJECTS+0x4200;
    w32(0,vtable+0x3c,V3C);w32(0,vtable+0x44,V44);w32(0,vtable+0x48,V48);
    /* Groups 0, 1 (side 0) and 2 (side 1). */
    w8(group(0)+0x12,0);w8(group(1)+0x12,0);w8(group(2)+0x12,1);
    for(unsigned k=0;k<3;++k){w32(0,group(k)+0xc,members+0x10*k);w16(0,group(k)+0x10,1);}
    for(unsigned k=0;k<6;++k){uint32_t a=actor(k);w32(0,a,vtable);w32(0,a+0x45,group(k<2?0:k<3?1:2));w16(0,a+0x4b,(uint16_t)k);
        w32(0,list+4*k,a);w16(0,a+0x1e6,1000);}
    w32(0,members,actor(0));w32(0,members+0x10,actor(2));w32(0,members+0x20,actor(3));
    w32(0,VA(0x4a9af8),list);w16(0,VA(0x4a9afe),6);
}
int main(void){
    es2_weapon_graph g=graph();
    uint32_t a0=actor(0),a1=actor(1),a2=actor(2),a3=actor(3),a4=actor(4),a5=actor(5);
    /* contact_share: same side returns Q's group and side. */
    reset();
    uint64_t v=es2_contact_share_registers(&memory,&g,a0,a1,entry);
    CHECK((uint32_t)v==group(0)&&(uint32_t)(v>>32)==0&&calls[2]==0);
    /* Allies of a0 (a0, a1, a2) learn a3 within 100000; within 50000 a3 is
     * marked and the triggers fire unless +A2h is set. */
    place(a0,0,0);place(a1,90000,0);place(a2,40000,0);place(a3,0,0);
    w32(0,a0+0x1b2,0x777);w32(0,a2+0x1b2,0x888);w8(a2+0xa2,1);
    v=es2_contact_share_registers(&memory,&g,a0,a3,entry);
    CHECK(knows(a0,a3)&&knows(a1,a3)&&knows(a2,a3)&&!knows(a4,a3)&&r8(a3+0x9e)==1&&calls[5]==1);
    CHECK(calls[2]==3&&(uint16_t)(v>>32)==6);
    /* contact_expire: out of range (> 100000) and out of sight drop both
     * entries; in range and in sight stays. */
    reset();place(a0,0,0);place(a3,100000,0);place(a4,100001,0);place(a5,10,0);
    for(unsigned k=3;k<6;++k){w8(a0+0xc2+k,1);w8(actor(k)+0xc2,1);}
    w8(a0+0x1f0,1);w8(a0+0x95,1);
    es2_contact_expire_registers(&memory,&g,a0,entry);
    CHECK(r8(a0+0x95)==0&&knows(a0,a3)&&knows(a3,a0)&&!knows(a0,a4)&&!knows(a4,a0)&&knows(a0,a5));
    CHECK(calls[2]==3&&calls[3]==2);
    w8(a0+0x1f0,0);es2_contact_expire_registers(&memory,&g,a0,entry);
    CHECK(!knows(a0,a3)&&!knows(a3,a0)&&!knows(a0,a5));
    /* contact_scan: mutual contact within 50000 marks both and fires. */
    reset();place(a0,0,0);place(a3,30000,0);place(a4,300000,0);place(a5,70000,0);
    w8(a0+0xc2+3,1);w8(a3+0xc2+0,1);w32(0,a0+0x1b2,0x777);w32(0,a3+0x1b2,0x999);
    v=es2_contact_scan_registers(&memory,&g,a0,entry);
    CHECK(r8(a0+0x9e)==1&&r8(a3+0x9e)==1&&calls[5]==2);
    /* a5 (70000, not known): heading, the facing test of both, no sight. */
    CHECK(calls[4]==1&&calls[6]==2&&calls[7]==2&&calls[3]==1&&calls[11]==0);
    /* With sight from a0 both contact_share calls run. */
    reset();place(a0,0,0);place(a3,500000,0);place(a4,500000,0);place(a5,70000,0);w8(a0+0x1f0,1);w16(0,a5+0x298,0x8000);
    es2_contact_scan_registers(&memory,&g,a0,entry);
    CHECK(calls[8]==1&&calls[11]==2&&knows(a0,a5)&&knows(a1,a5)&&knows(a5,a0)&&!knows(a3,a0)&&!knows(a4,a0));
    /* Player A (+A3h) records the contact itself. */
    reset();place(a0,0,0);place(a3,500000,0);place(a4,500000,0);place(a5,70000,0);w8(a0+0x1f0,1);w8(a0+0xa3,1);
    w16(0,a5+0x298,0x8000);
    es2_contact_scan_registers(&memory,&g,a0,entry);
    CHECK(calls[11]==1&&knows(a0,a5)&&!knows(a1,a5)&&knows(a5,a0));
    /* Alert: a0 alert-capable (+96h), a3 at 150000 in sight: a0 alerted, V48 not called. */
    reset();place(a0,0,0);place(a3,150000,0);place(a4,500000,0);place(a5,500000,0);w8(a0+0x1f0,1);w8(a0+0x96,1);
    es2_contact_scan_registers(&memory,&g,a0,entry);
    CHECK(r8(a0+0x95)==1&&r8(a3+0x95)==0&&calls[8]==0);
    w8(a3+0x96,1);w8(a0+0x95,0);es2_contact_scan_registers(&memory,&g,a0,entry);
    CHECK(r8(a0+0x95)==1&&r8(a3+0x95)==1&&calls[8]==1);
    /* actor_pass: timers in order, re-arm 10007 on expiry, the held player
     * scanned last, +A2h cleared everywhere. */
    reset();for(unsigned k=0;k<6;++k){place(actor(k),500000*(int32_t)k,0);w8(actor(k)+0xa2,1);}
    w16(0,a1+0x1e6,1);w8(a0+0xa3,1);w32(0,group(2)+0x14,0);w32(0,group(1)+0x14,1);
    v=es2_actor_pass_registers(&memory,&g,entry);
    CHECK(r16(0,a1+0x1e6)==10007&&r16(0,a0+0x1e6)==999&&r16(0,a2+0x1e6)==1000&&calls[1]==1&&calls[9]==1);
    CHECK(calls[0]==10&&calls[10]==2);
    for(unsigned k=0;k<6;++k)CHECK(r8(actor(k)+0xa2)==0);
    CHECK(scans==2&&scanned[0]==a1&&scanned[1]==a0&&(uint32_t)v==a5+0x92&&(uint32_t)(v>>32)==7);
    /* A null entry ends a loop; a negative count skips all. */
    reset();w32(0,OBJECTS+0x4200+8,0);es2_actor_pass_registers(&memory,&g,entry);CHECK(calls[0]==4);
    reset();w16(0,VA(0x4a9afe),0x8000);v=es2_actor_pass_registers(&memory,&g,entry);
    CHECK(calls[0]==0&&(uint32_t)v==OBJECTS+0x4200&&(uint32_t)(v>>32)==1);
    printf("PASS: actor_pass cores, %u checks\n",checks);
    return 0;
}
