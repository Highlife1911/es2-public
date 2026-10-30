#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/collision.h"
/* Synthetic checks of the core on an own arena with scripted callees:
 * set_length (scaled and zero vector), grid_sweep (empty grid, segment
 * above the grid, a hit on the third sample, no hit) and object_query (no
 * entries, an object out of reach, a hit that ends the loop below 500).
 * Values are hand-derived; the in-vitro harness compares against the
 * original. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x00400000)
static uint8_t arena[8192], image_bytes[0x2d8000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"object_query line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena)||a-IMAGE<sizeof(image_bytes));
    return a-BASE<sizeof(arena)?arena+(a-BASE):image_bytes+(a-IMAGE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define SCRATCH (BASE+0x1800u)
static unsigned calls;
static uint32_t functions[64],first_args[64];
static es2_weapon_registers entries[64];
static uint32_t heights[8],distances[4];static unsigned height_at,distance_at;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);functions[calls]=f;entries[calls]=*r;first_args[calls]=a[0];++calls;
    switch(f){
    case 0x49681a:CHECK(n==3&&a[1]==0&&a[2]==12);memset(host(0,a[0]),0,12);r->eax=a[0];r->edx=12;r->ecx=0;break;
    case 0x4927f0:CHECK(n==2);w32(0,a[0],16);w32(0,a[0]+4,0);w32(0,a[0]+8,0);r->eax=a[0];break;
    case 0x47de72:CHECK(n==3);r->eax=64;break;
    case 0x427238:CHECK(n==3);r->eax=heights[height_at++];r->edx=a[0];break;
    case 0x49288c:CHECK(n==2);r->eax=distances[distance_at++];r->edx=0x5555;break;
    case 0x48043c:CHECK(n==3);
        /* The object position in segment space: inside the box. */
        w32(0,a[2],5);w32(0,a[2]+4,40);w32(0,a[2]+8,0);r->eax=0x77770000u;break;
    case 0x47ebb8:case 0x47df19:case 0x480304:break;
    case 0x4273c8:CHECK(n==4&&(uint16_t)a[3]==7);w32(0,a[1]+4,1);w32(0,a[1]+8,2);w32(0,a[1]+12,3);r->eax=0x00010001u;r->edx=0x6666;break;
    default:fprintf(stderr,"unexpected callee %08x\n",(unsigned)f);CHECK(0);
    }
}
static es2_weapon_graph graph(void){
    es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,NULL,NULL,NULL};
    return g;
}
static uint32_t scratch_address(void){return SCRATCH;}
static void set_length_cases(void){
    es2_weapon_graph g=graph();uint32_t v=BASE+0x100;
    w32(0,v,3000);w32(0,v+4,4000);w32(0,v+8,0);
    /* Octagonal length 4000 + 750 + 187 + 93 = 5030; q = 65536000 / 5030 = 13029. */
    CHECK(es2_set_length_registers(&memory,&g,v,1000)==(uint64_t)v);
    CHECK(r32(0,v)==596&&r32(0,v+4)==795&&r32(0,v+8)==0&&calls==0);
    /* Length 3000, q = 2184; x floors to -100, z = -1 with EDX = high dword -1. */
    w32(0,v,0xfffff448u);w32(0,v+4,0);w32(0,v+8,0xffffffffu);
    CHECK(es2_set_length_registers(&memory,&g,v,0x12340064u)==((uint64_t)0xffffffffu<<32|v));
    CHECK(r32(0,v)==0xffffff9cu&&r32(0,v+4)==0&&r32(0,v+8)==0xffffffffu);
    /* Zero vector: memset through the bridge, EDX from the runtime. */
    w32(0,v,0);w32(0,v+4,0);w32(0,v+8,0);calls=0;
    CHECK(es2_set_length_registers(&memory,&g,v,50)==((uint64_t)12<<32|v));
    CHECK(calls==1&&functions[0]==0x49681a&&entries[0].eax==0&&entries[0].edx==0&&entries[0].ecx==v);
}
static void grid_sweep_cases(void){
    es2_weapon_graph g=graph();uint32_t s=BASE+0x400,seg=BASE+0x200,out=BASE+0x300;const uint32_t entry[3]={0xaaaa,0xbbbb,0xcccc};
    memset(arena,0,sizeof(arena));calls=0;
    CHECK(es2_grid_sweep_registers(&memory,&g,s,out,seg,0,entry)==((uint64_t)0xbbbb<<32));
    /* Above the grid: start.z and end.z both at or above the bound. */
    w16(0,s+0x2a,4);w16(0,s+0x2c,4);w32(0,s+0x430,100);w32(0,seg+8,100);w32(0,seg+0x14,150);
    CHECK(es2_grid_sweep_registers(&memory,&g,s,out,seg,0,entry)==((uint64_t)100<<32)&&calls==0);
    /* Shift 4, origin (2, 3) cells = (32, 48); step 16, D scaled to (16, 0, 0);
     * length 64 gives five samples; the third (x = 64) is below height 150. */
    w32(0,s+0x430,1000);w16(0,s+0x2e,2);w16(0,s+0x30,3);w16(0,s+0x32,4);
    w32(0,seg,0);w32(0,seg+4,0);w32(0,seg+8,100);w32(0,seg+12,64);w32(0,seg+16,0);w32(0,seg+20,100);
    heights[0]=0;heights[1]=50;heights[2]=150;height_at=0;
    uint64_t r=es2_grid_sweep_registers(&memory,&g,s,out,seg,0x12340000u,entry);
    CHECK(r==((uint64_t)48<<32|(((out+4)&0xffff0000u)|1u)));
    CHECK(r32(0,out+4)==32&&r32(0,out+8)==0&&r32(0,out+12)==100&&r32(0,out)==0);
    CHECK(calls==5&&functions[0]==0x4927f0&&functions[1]==0x47de72&&functions[2]==0x427238);
    CHECK(entries[0].edx==32&&entries[0].ecx==48);                       /* A.x, A.y */
    CHECK(entries[1].eax==0&&entries[1].edx==0&&entries[1].ecx==(uint32_t)-64);
    CHECK(entries[2].eax==0&&entries[2].edx==0&&entries[2].ecx==16);    /* n | radius, remainder, step */
    CHECK(entries[3].eax==0&&entries[3].edx==0&&entries[3].ecx==0);     /* D.x high word | radius, D.y, D.z */
    /* No hit: five samples, EDX = D.y. */
    for(unsigned i=0;i<5;++i)heights[i]=0;height_at=0;calls=0;
    CHECK(es2_grid_sweep_registers(&memory,&g,s,out,seg,0,entry)==0&&calls==7);
}
static void object_query_cases(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={1,2,3};
    uint32_t out=BASE+0x600,p=BASE+0x640,list=BASE+0x700,b=BASE+0x800,holder=BASE+0x900,shape=BASE+0x980;
    memset(arena,0,sizeof(arena));calls=0;
    CHECK(es2_object_query_registers(&memory,&g,out,p,list,0,entry)==((uint64_t)list<<32|(list&0xffff0000u)));
    CHECK(es2_object_query_registers(&memory,&g,out,p,list,0xffff,entry)==((uint64_t)list<<32|(list&0xffff0000u))&&calls==0);
    w32(0,list,b);w32(0,b+0x34,holder);w32(0,holder+4,shape);w16(0,shape+8,10);
    w32(0,p,BASE+0xa00);w32(0,p+4,1000);w16(0,p+8,7);
    w32(0,IMAGE+0xaaad0,5000);
    /* Out of reach (radius 17, reach 1017): the nearest global takes 2000. */
    distances[0]=2000;distance_at=0;
    uint64_t r=es2_object_query_registers(&memory,&g,out,p,list,1,entry);
    CHECK(r==((uint64_t)1<<32)&&r32(0,IMAGE+0xaaad0)==2000&&calls==1&&entries[0].ecx==BASE+0xa14);
    /* In reach, local (5, 40) inside, 4273c8 hits, 300 < 500 ends the loop. */
    distances[0]=900;distances[1]=300;distance_at=0;calls=0;
    r=es2_object_query_registers(&memory,&g,out,p,list,2,entry);
    CHECK(r==((uint64_t)out<<32|1u));
    CHECK(r32(0,p+4)==300&&r32(0,out)==300&&r32(0,out+4)==b+0xc&&r32(0,IMAGE+0xaaad0)==0&&r16(0,b+0x32)==1);
    static const uint32_t order[]={0x49288c,0x48043c,0x47ebb8,0x47df19,0x480304,0x48043c,0x48043c,0x48043c,0x4273c8,0x49288c};
    CHECK(calls==10);for(unsigned i=0;i<10;++i)CHECK(functions[i]==order[i]);
    uint32_t f=scratch_address()+0xa0u;
    CHECK(entries[8].eax==shape&&entries[8].edx==f-0x84u&&entries[8].ecx==f-0x6cu);
    CHECK(entries[9].eax==0x00010001u&&first_args[9]==f-0x84u);
}
int main(void){
    set_length_cases();grid_sweep_cases();object_query_cases();
    printf("object_query core: %u checks passed\n",checks);return 0;
}
