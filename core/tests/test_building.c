#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/building.h"
/* Synthetic state for the cores. The image base is placed so that the
 * globals (49933c .. 4a9e70 + 2000) fall inside the arena; objects live
 * above them. target_valid and group_death resolve to the cores themselves,
 * the other callees to small models; the factor tables hold synthetic
 * values (1024 = 1.0), not the image's. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x99000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x40000u)
#define VIRTUAL UINT32_C(0x12345678)
static uint8_t arena[0x48000];static unsigned checks,calls[8];static uint32_t order_target,kind;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"building line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static es2_weapon_graph graph(void);
static uint32_t magnitude(uint32_t v){return (v&0x80000000u)?0u-v:v;}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;es2_weapon_graph g=graph();uint64_t p=0;uint32_t entry[3]={r->eax,r->edx,r->ecx};
    if(f==VA(0x49288c)){CHECK(n==2);++calls[0];  /* metric model: max of |dx|,|dy| */
        uint32_t x=magnitude(r32(0,a[0])-r32(0,a[1])),y=magnitude(r32(0,a[0]+4)-r32(0,a[1]+4));p=x>y?x:y;}
    else if(f==VA(0x492934)){CHECK(n==2);++calls[1];p=0;}         /* heading model: always 0 */
    else if(f==VA(0x47e0b0)){CHECK(n==2);++calls[2];p=(uint32_t)(((int32_t)a[0]*(int32_t)a[1])>>10);}
    else if(f==VA(0x411c58)){CHECK(n==2);++calls[3];p=1;}         /* every target is known */
    else if(f==VA(0x4238a0)){CHECK(n==1);++calls[4];p=order_target;}
    else if(f==VA(0x423918)){CHECK(n==2);++calls[5];p=0;}         /* no order subject */
    else if(f==VIRTUAL){CHECK(n==2);++calls[6];p=kind;}
    else if(f==VA(0x411e80)){p=es2_target_valid_registers(&memory,&g,a[0],a[1],a[2],entry);}
    else if(f==VA(0x423f30)){++calls[7];p=es2_group_death_registers(&memory,&g,a[0],a[1],entry);}
    else CHECK(0);
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);
}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,0,0,0,0,call,0,0,0};return g;}
/* Objects: actors 0x40000 (stride 0x200), groups 0x42000 (0x7a), a vtable
 * at 0x43000 and member arrays at 0x43100. */
static uint32_t actor(unsigned i){return OBJECTS+0x200u*i;}
static uint32_t group(unsigned i){return OBJECTS+0x2000u+0x7au*i;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static uint16_t variable(unsigned i){return r16(0,VA(0x4a9e70)+2u*i);}
static void place(uint32_t a,int32_t x,int32_t y){w32(0,a+0x26,(uint32_t)x);w32(0,a+0x2a,(uint32_t)y);w32(0,a+0x2e,0);}
int main(void){
    es2_weapon_graph g=graph();
    uint32_t g0=group(0),g1=group(1),vtable=OBJECTS+0x3000,members=OBJECTS+0x3100;
    for(unsigned k=0;k<6;++k){uint32_t a=actor(k);w32(0,a,vtable);w32(0,a+0x45,k<2?g0:g1);}
    w32(0,vtable+0x4c,VIRTUAL);
    w8(g0+0x12,1);w8(g1+0x12,2);
    w32(0,g0+0xc,members);w16(0,g0+0x10,2);w32(0,members,actor(0));w32(0,members+4,actor(1));
    w32(0,g1+0xc,members+0x10);w16(0,g1+0x10,4);for(unsigned k=0;k<4;++k)w32(0,members+0x10+4*k,actor(2+k));
    for(unsigned k=0;k<10;++k){w16(0,g0+0x1c+2*k,0xffff);w16(0,actor(2)+0x1ba+2*k,0xffff);}
    /* group_death: another living member stops at its index. */
    CHECK(es2_group_death_registers(&memory,&g,g0,actor(0),entry)==(uint64_t)1<<32);
    /* All others destroyed: clear, increment, store action-12, skip. */
    w8(actor(1)+0x99,1);
    w16(0,g0+0x1c,3);w16(0,g0+0x30,1);w16(0,VA(0x4a9e70)+6,77);
    w16(0,g0+0x1e,4);w16(0,g0+0x32,2);w16(0,VA(0x4a9e70)+8,0xffff);
    w16(0,g0+0x20,5);w16(0,g0+0x34,15);
    w16(0,g0+0x22,6);w16(0,g0+0x36,17);w16(0,VA(0x4a9e70)+12,9);
    w16(0,g0+0x24,0x8007);w16(0,g0+0x38,1);
    CHECK(es2_group_death_registers(&memory,&g,g0,actor(0),entry)==((uint64_t)10<<32|(g0+0x30)));
    CHECK(variable(3)==0&&variable(4)==0&&variable(5)==3&&variable(6)==9);
    /* death_actions: the group first, then the actor's own table. */
    w16(0,actor(2)+0x1ba,7);w16(0,actor(2)+0x1ce,13);
    CHECK(es2_death_actions_registers(&memory,&g,actor(2),entry)==((uint64_t)(actor(2)+0x1e2)<<32|(actor(2)+0x1ce)));
    CHECK(calls[7]==1&&variable(7)==1);
    w32(0,actor(5)+0x45,0);
    CHECK(es2_death_actions_registers(&memory,&g,actor(5),entry)==((uint64_t)(actor(5)+0x1e2)<<32|(actor(5)+0x1ce)));
    CHECK(calls[7]==1);w32(0,actor(5)+0x45,g1);
    /* target_valid: same category refused, enemy accepted with AX = 1. */
    uint32_t a0=actor(0),a2=actor(2),a3=actor(3);
    CHECK((uint32_t)es2_target_valid_registers(&memory,&g,a0,actor(1),0,entry)==0);
    uint64_t v=es2_target_valid_registers(&memory,&g,a0,a2,0,entry);
    CHECK((uint32_t)v==(((a0+0x92)&0xffff0000u)|1u)&&(uint32_t)(v>>32)==a0+0x92);
    /* Type 2 candidates must be alive; equal types refused with flag 10h. */
    w16(0,a2+0x1a8,2);w8(a2+0xa5,1);CHECK(es2_target_valid_registers(&memory,&g,a0,a2,0,entry)==(uint64_t)1<<32);
    w8(a2+0xa5,0);w16(0,a0+0x1a8,2);CHECK((uint16_t)es2_target_valid_registers(&memory,&g,a0,a2,0,entry)==1);
    CHECK((uint32_t)es2_target_valid_registers(&memory,&g,a0,a2,0x10,entry)==0);w16(0,a0+0x1a8,0);w16(0,a2+0x1a8,0);
    /* The current order's target in mode 3, and the cockpit's target. */
    w8(a0+0xa3,1);w16(0,VA(0x4a9e54),3);order_target=a2;
    CHECK((uint32_t)es2_target_valid_registers(&memory,&g,a0,a2,0,entry)==0&&calls[4]==1);
    w8(a0+0xa3,0);
    uint32_t cockpit=OBJECTS+0x3200;w32(0,VA(0x49b088),cockpit);w32(0,cockpit+0x210,a3);w8(a0+0x9a,1);
    CHECK(es2_target_valid_registers(&memory,&g,a0,a3,0,entry)==(uint64_t)cockpit<<32);
    CHECK(es2_target_valid_registers(&memory,&g,a0,a2,0,entry)==((uint64_t)cockpit<<32|((a0+0x92)&0xffff0000u)|1u));
    w8(a0+0x9a,0);
    /* target_select: unit factors, the nearest enemy wins. */
    for(unsigned k=0;k<3;++k){w16(0,VA(0x49933c)+2*k,1024);w16(0,VA(0x499342)+2*k,1024);w16(0,VA(0x499348)+2*k,1024);}
    for(unsigned k=0;k<4;++k)w16(0,VA(0x49934e)+2*k,1024);
    uint32_t list=OBJECTS+0x3300;w32(0,VA(0x4a9af8),list);w16(0,VA(0x4a9afe),6);
    for(unsigned k=0;k<6;++k)w32(0,list+4*k,actor(k));
    place(a0,0,0);place(actor(1),10,0);place(a2,50000,0);place(a3,8000,0);place(actor(4),200000,0);place(actor(5),30000,0);
    w8(actor(5)+0x99,1);kind=0;
    v=es2_target_select_registers(&memory,&g,a0,0,0,entry);
    CHECK((uint32_t)v==a3);
    /* Beyond 100000 nothing qualifies; the arc rejects nothing at heading 0. */
    place(a2,150000,0);place(a3,150000,0);place(actor(5),150000,0);
    CHECK((uint32_t)es2_target_select_registers(&memory,&g,a0,0x20,0x100,entry)==0);
    printf("PASS: building cores, %u checks\n",checks);
    return 0;
}
