#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mission.h"
/* Synthetic mission state; the image base is placed so that the mission
 * globals (49b088, 4a9e00..4aa640) fall inside the arena. Callees are resolved to
 * the core functions themselves, the distance to the octagonal metric. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x9b000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
static uint8_t arena[0x10000];static unsigned checks,messages,calls[8];static uint16_t seen[16];static uint8_t record[22];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mission line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint8_t scratch_record[32];
static es2_weapon_graph graph(void);
static uint32_t magnitude(uint32_t v){return (v&0x80000000u)?0u-v:v;}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;es2_weapon_graph g=graph();uint64_t p=0;uint32_t entry[3]={r->eax,r->edx,r->ecx};
    if(f==VA(0x4928d0)){CHECK(n==2);++calls[0];uint32_t x=magnitude(r32(0,a[0])-r32(0,a[1])),y=magnitude(r32(0,a[0]+4)-r32(0,a[1]+4));
        p=x>y?x+y/2:y+x/2;r->ecx=0x5150u;}
    else if(f==VA(0x4233a4)){CHECK(n==2);++calls[1];p=es2_region_test_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x4234b8)){CHECK(n==2);++calls[2];p=es2_trigger_regions_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x423508)){CHECK(n==2);++calls[3];p=es2_trigger_group_registers(&memory,&g,a[0],a[1],entry);}
    else if(f==VA(0x423430)){CHECK(n==1);++calls[4];p=es2_trigger_fire_registers(&memory,&g,a[0],0,entry);r->ecx=a[0];}
    else if(f==VA(0x433158)){CHECK(!n);uint32_t o=r32(0,VA(0x49b088));p=(uint64_t)r->edx<<32|(o?r32(0,o+0x207):0);}
    else if(f==0x1234u){CHECK(n==2&&r->eax==BASE+0x3080&&r->ecx==a[1]&&!r->edx);memcpy(record,host(0,a[1]),22);
        if(messages<16)seen[messages]=r16(0,a[1]);++messages;p=0x00ab0000cd000000ull;}
    else CHECK(0);
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);
}
static es2_weapon_graph graph(void){
    es2_weapon_graph g={IMAGE,(uint32_t)(BASE+0x3800),0,0,0,call,0,0,0};(void)scratch_record;return g;
}
/* Layout: points 0x100, regions 0x200, region lists 0x280, triggers 0x400,
 * groups 0x800, actor arrays 0x980, actors 0xa00 (stride 0x80), cockpit
 * 0x3000, record scratch 0x3800; globals at arena 0x88 and 0xee00.. */
static uint32_t point(unsigned i,uint32_t x,uint32_t y){uint32_t p=BASE+0x100+12*i;w32(0,p,x);w32(0,p+4,y);w32(0,p+8,7);return p;}
static uint32_t region(unsigned i,uint16_t type,uint32_t first,uint32_t second){uint32_t q=BASE+0x200+10*i;w16(0,q,type);w32(0,q+2,first);w32(0,q+6,second);return q;}
static uint32_t trigger(unsigned i,uint16_t type){uint32_t t=BASE+0x400+0x3a*i;memset(host(0,t),0,0x3a);w16(0,t,type);
    for(unsigned j=0;j<10;++j){w16(0,t+0xc+2*j,0xffff);w16(0,t+0x20+2*j,0xffff);}w16(0,t+0x34,0xffff);return t;}
static uint32_t actor(unsigned i,uint32_t x,uint32_t y){uint32_t a=BASE+0xa00+0x80*i;w32(0,a+0x26,x);w32(0,a+0x2a,y);return a;}
static uint32_t entry_default[3]={0x11112222u,0x33334444u,0x55556666u};
int main(void){
    es2_weapon_graph g=graph();
    /* Rectangle (corners swapped on purpose): strict interior only. */
    uint32_t c1=point(0,1000,-500),c2=point(1,-1000,500),pos=BASE+0x180;
    uint32_t box=region(0,0,c1,c2);
    static const int32_t inside[][3]={{0,0,1},{999,499,1},{-999,-499,1},{1000,0,0},{-1000,0,0},{0,500,0},{0,-500,0},{2000,0,0},{0,9999,0}};
    for(unsigned i=0;i<sizeof(inside)/sizeof(inside[0]);++i){w32(0,pos,(uint32_t)inside[i][0]);w32(0,pos+4,(uint32_t)inside[i][1]);
        uint64_t r=es2_region_test_registers(&memory,&g,box,pos,entry_default);CHECK(((uint16_t)r!=0)==inside[i][2]);
        if(inside[i][2])CHECK((uint32_t)r==((r32(0,c2+4)&0xffff0000u)|1u));else CHECK((uint32_t)r==0);}
    /* Distance region: threshold 100 (serialized 10), strictly below. */
    uint32_t centre=point(2,5000,5000),ring=region(1,1,centre,100);
    w32(0,pos,5099);w32(0,pos+4,5000);CHECK((uint16_t)es2_region_test_registers(&memory,&g,ring,pos,entry_default)==1);
    w32(0,pos,5100);CHECK(!(uint32_t)es2_region_test_registers(&memory,&g,ring,pos,entry_default));
    w32(0,pos,5060);w32(0,pos+4,5060);CHECK((uint16_t)es2_region_test_registers(&memory,&g,ring,pos,entry_default)==1); /* 60+30 */
    w32(0,pos,5070);w32(0,pos+4,5070);CHECK(!(uint32_t)es2_region_test_registers(&memory,&g,ring,pos,entry_default)); /* 70+35 */
    /* Mission globals and the cockpit object for messages. */
    w32(0,VA(0x49b088),BASE+0x3100);w32(0,BASE+0x3100+0x207,BASE+0x3000);w32(0,BASE+0x3000,BASE+0x3080);w32(0,BASE+0x3080,0x1234u);
    uint32_t bank=VA(0x4a9e70);
    /* trigger_fire: actions 6 (increment), 5 (clear), 4 (ignored); one message per used slot. */
    uint32_t t=trigger(0,0);w16(0,t+0xc,3);w16(0,t+0x20,6);w16(0,t+0xe,4);w16(0,t+0x22,5);w16(0,t+0x10,9);w16(0,t+0x24,4);w16(0,t+0x34,0x1c);
    w16(0,bank+6,0xffff);w16(0,bank+8,77);w16(0,bank+18,5);
    uint8_t residue[16];for(unsigned i=0;i<16;++i)residue[i]=(uint8_t)(0xa0+i);memcpy(host(0,BASE+0x3900),residue,16);
    messages=0;uint64_t r=es2_trigger_fire_registers(&memory,&g,t,BASE+0x3900,entry_default);
    CHECK(r16(0,t+0xa)==1&&r16(0,bank+6)==0&&r16(0,bank+8)==0&&r16(0,bank+18)==5&&messages==3&&seen[0]==0x1c);
    CHECK(!memcmp(record+6,residue,16)&&r32(0,BASE+0x3802)==0);
    CHECK(r==0x00ab0000cd00ffffull); /* unused slots load AX = -1 after the last message */
    messages=0;r=es2_trigger_fire_registers(&memory,&g,t,0,entry_default);CHECK(!messages&&r==((uint64_t)entry_default[1]<<32|entry_default[0]));
    /* Selector -1: actions only. */
    t=trigger(1,0);w16(0,t+0xc,3);w16(0,t+0x20,6);messages=0;(void)es2_trigger_fire_registers(&memory,&g,t,0,entry_default);CHECK(!messages&&r16(0,bank+6)==1);
    /* trigger_scan over five triggers: region (type 0), player group (1),
     * class-word groups (4), subject actor (7) and an empty subject group (10). */
    memset(calls,0,sizeof(calls));
    uint32_t player=actor(0,0,0),wing=actor(1,5050,5000),list=BASE+0x280;
    w32(0,list,ring);w32(0,list+4,box);
    t=trigger(0,0);w16(0,t+4,2);w32(0,t+6,list);w16(0,t+0xc,1);w16(0,t+0x20,6);
    uint32_t t1=trigger(1,1);w16(0,t1+4,1);w32(0,t1+6,list);w16(0,t1+0xc,2);w16(0,t1+0x20,6);
    uint32_t t2=trigger(2,4);w16(0,t2+4,1);w32(0,t2+6,list);w16(0,t2+0xc,3);w16(0,t2+0x20,6);
    uint32_t t3=trigger(3,7);w16(0,t3+4,1);w32(0,t3+6,list);w32(0,t3+0x36,wing);w16(0,t3+0xc,4);w16(0,t3+0x20,6);
    uint32_t t4=trigger(4,10);w32(0,t4+0x36,BASE+0x800+2*0x7a);w16(0,t4+0xc,5);w16(0,t4+0x20,6);
    /* Groups: 0 = player and wing (class 1), 1 = wing only (class 0, pending), 2 = empty (class 2). */
    memset(host(0,BASE+0x800),0,3*0x7a);
    w16(0,BASE+0x800,1);w32(0,BASE+0x800+0xc,BASE+0x980);w16(0,BASE+0x800+0x10,2);w32(0,BASE+0x980,player);w32(0,BASE+0x984,wing);
    w16(0,BASE+0x87a,0);w32(0,BASE+0x87a+0xc,BASE+0x990);w16(0,BASE+0x87a+0x10,1);w32(0,BASE+0x990,wing);w32(0,BASE+0x87a+0x14,1);
    w16(0,BASE+0x800+2*0x7a,2);w32(0,player+0x45,BASE+0x800);
    w32(0,VA(0x4a9e28),BASE+0x400);w16(0,VA(0x4a9e24),5);w32(0,VA(0x4a9e00),BASE+0x800);w16(0,VA(0x4a9e04),3);
    for(unsigned i=1;i<6;++i)w16(0,bank+2*i,0);
    messages=0;r=es2_trigger_scan_registers(&memory,&g,player,entry_default);
    /* Player at the origin is inside the box: t fires. The player group's
     * wing is inside the ring: t1 fires. Type 4 wants class 0 and finds
     * only the pending group 1: no fire. Type 7 (wing) fires. Type 10 with
     * the empty group returns the high word of the ECX left by t3's fire
     * (the trigger pointer), nonzero: fires. */
    CHECK(r16(0,bank+2)==1&&r16(0,bank+4)==1&&!r16(0,bank+6)&&r16(0,bank+8)==1&&r16(0,bank+10)==1);
    CHECK(!r16(0,t2+0xa)&&r16(0,t4+0xa)==1);
    /* Type 6 picks the empty class-2 group: 423508 returns the high word of
     * ECX = group pointer, so the empty group fires the trigger. */
    w16(0,t2,6);w16(0,VA(0x4a9e24),5);r=es2_trigger_scan_registers(&memory,&g,player,entry_default);CHECK(r16(0,t2+0xa)==1&&r16(0,bank+6)==1);
    CHECK((uint32_t)(r>>32)==5);
    /* Second pass: latched region triggers call 423430 again, which returns at once. */
    unsigned fires=calls[4];r=es2_trigger_scan_registers(&memory,&g,player,entry_default);
    CHECK(r16(0,bank+2)==1&&calls[4]>fires);
    /* Empty count: entry EAX survives, EDX = 0. */
    w16(0,VA(0x4a9e24),0);r=es2_trigger_scan_registers(&memory,&g,player,entry_default);CHECK(r==(uint64_t)entry_default[0]);
    /* Empty group with entry ECX: AX = ECX >> 16. */
    r=es2_trigger_group_registers(&memory,&g,BASE+0x800+2*0x7a,t4,entry_default);CHECK(r==((uint64_t)entry_default[1]<<32|0x11115555u));
    puts("PASS: mission triggers: strict rectangle, distance threshold, actions and per-slot messages with residue, latch, scan types 0/1/4/6/7/10, empty-group residue");
    printf("%u checks\n",checks);return 0;
}
