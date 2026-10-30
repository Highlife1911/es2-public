#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/col.h"
/* Synthetic state for the core (col_query 40c9d4, col_group 40c8fc,
 * col_shape 40c8c8, col_bound 40c4c4). The image base is placed so that the
 * statics 4a980c..4a9823 fall inside the arena. The game callees are small
 * logging models: 4801d4 adds the center words to the matrix translation,
 * dist_octagonal 47de4c is max + min/2 of the magnitudes, matrix_compose
 * 47fa20 writes a pattern, sphere_hits 40c524 returns a per-test value, and
 * the node lookup returns a per-test node. Calls of the group's own
 * functions run the core again (with an own scratch per depth), so the
 * tests also cover the composition. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x13000u)
#define COL OBJECTS
#define GROUPS (OBJECTS+0x40u)
#define SHAPES (OBJECTS+0x100u)
#define ACTOR (OBJECTS+0x300u)
#define INSTANCE (OBJECTS+0x380u)
#define LOOKUP (OBJECTS+0x3c0u)
#define VTABLE (OBJECTS+0x400u)
#define NODE (OBJECTS+0x480u)
#define NODES (OBJECTS+0x500u)
#define MATRIX (OBJECTS+0x600u)
#define SEGMENT (OBJECTS+0x640u)
#define FLAGS (OBJECTS+0x680u)
#define SCRATCH (OBJECTS+0xc00u)
#define V20 UINT32_C(0x12345678)
static uint8_t arena[0x14000];static unsigned checks,ncalls;
static uint32_t calls[32],args[32][4],regs[32][3],sphere_result,node_result;
static int nested;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"col line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static int32_t s16(uint16_t v){return v<0x8000u?(int32_t)v:(int32_t)v-0x10000;}
static uint32_t mag(uint32_t v){return (int32_t)v<0?0u-v:v;}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r);
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;}
static es2_weapon_graph graph(int depth){
    es2_weapon_graph g={IMAGE,SCRATCH+0x40u*(uint32_t)depth,0,0,0,call,0,observe,0};return g;
}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<32);const unsigned k=ncalls++;calls[k]=f;
    for(unsigned i=0;i<n&&i<4;++i)args[k][i]=a[i];
    regs[k][0]=r->eax;regs[k][1]=r->edx;regs[k][2]=r->ecx;
    ++nested;es2_weapon_graph g=graph(nested);uint64_t v;
    if(f==VA(0x40c8fc)){v=es2_col_group_registers(&memory,&g,a[0],a[1],a[2],a[3],regs[k]);r->eax=(uint32_t)v;r->edx=(uint32_t)(v>>32);r->ecx=0xc1;}
    else if(f==VA(0x40c8c8)){v=es2_col_shape_registers(&memory,&g,a[0],regs[k]);r->eax=(uint32_t)v;r->edx=(uint32_t)(v>>32);r->ecx=0xc2;}
    else if(f==VA(0x40c4c4)){v=es2_col_bound_registers(&memory,&g,a[0],a[1],regs[k]);r->eax=(uint32_t)v;r->edx=(uint32_t)(v>>32);r->ecx=0xc3;}
    else if(f==VA(0x4801d4)){
        CHECK(n==3);
        for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,(uint32_t)s16(r16(0,a[0]+2*i))+r32(0,a[1]+0x14+4*i));
        r->edx=0xd1;r->ecx=0xd2;r->eax=a[2];
    } else if(f==VA(0x47de4c)){
        CHECK(n==2);uint32_t x=mag(a[0]),z=mag(a[1]);
        r->eax=x>=z?x+(z>>1):z+(x>>1);r->edx=0xe1;r->ecx=0xe2;
    } else if(f==VA(0x40c524)){CHECK(n==2&&a[1]==0);r->eax=sphere_result;r->edx=0xf1;r->ecx=0xf2;}
    else if(f==VA(0x47fa20)){CHECK(n==3);for(unsigned i=0;i<32;++i)arena[a[2]-BASE+i]=(uint8_t)(0x40+i);r->eax=a[2];r->edx=0xa1;r->ecx=0xa2;}
    else {CHECK(f==V20&&n==2);r->eax=node_result;r->edx=0xb1;}
    --nested;
}
static void setup(void){
    memset(arena,0x5a,sizeof(arena));ncalls=0;nested=0;
    w32(0,ACTOR+0x34,INSTANCE);w32(0,INSTANCE+4,LOOKUP);w32(0,INSTANCE+0x16,NODES);w32(0,LOOKUP,VTABLE);w32(0,VTABLE+0x20,V20);
    /* Matrix: translation (100, 200, 300), mode byte 0. */
    w32(0,MATRIX+0x14,100);w32(0,MATRIX+0x18,200);w32(0,MATRIX+0x1c,300);
    w32(0,VA(0x4a980c),MATRIX);w32(0,VA(0x4a9810),1000);w16(0,VA(0x4a9814),50);
}
/* Shape k: count 1, region word, center (x, y, z), radius. */
static void shape(unsigned k,uint16_t region,int16_t x,int16_t y,int16_t z,uint16_t radius){
    uint32_t s=SHAPES+0x10u*k;w16(0,s,1);w32(0,s+2,0);w16(0,s+6,region);
    w16(0,s+8,(uint16_t)x);w16(0,s+10,(uint16_t)y);w16(0,s+12,(uint16_t)z);w16(0,s+14,radius);
}
int main(void){
    es2_weapon_graph g=graph(0);uint64_t v;
    /* col_bound: center (0, 0, 0) -> (100, 200, 300); b = 20 + 1000 > 200;
     * d = 300 + 50 = 350 < 20 + 50? no: miss with EDX = r + R. */
    setup();w16(0,SHAPES+8,0);w16(0,SHAPES+10,0);w16(0,SHAPES+12,0);
    const uint32_t e[3]={0x11112222u,0x33334444u,0x55556666u};
    v=es2_col_bound_registers(&memory,&g,SHAPES+8,0xabcd0014u,e);
    CHECK(v==((uint64_t)70<<32|0));
    CHECK(ncalls==2&&calls[0]==VA(0x4801d4)&&args[0][0]==SHAPES+8&&args[0][1]==MATRIX&&args[0][2]==SCRATCH);
    CHECK(regs[0][0]==MATRIX&&regs[0][1]==SHAPES+8&&regs[0][2]==SCRATCH);
    CHECK(calls[1]==VA(0x47de4c)&&args[1][0]==100&&args[1][1]==300&&regs[1][0]==1020&&regs[1][1]==0xd1&&regs[1][2]==0xd2);
    /* A hit: translation (10, 0, 10), radius 20, R 50: d = 15 < 70; AX = 1,
     * EAX's high word from d. */
    setup();w32(0,MATRIX+0x14,10);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,10);
    w16(0,SHAPES+8,0);w16(0,SHAPES+10,0);w16(0,SHAPES+12,0);
    v=es2_col_bound_registers(&memory,&g,SHAPES+8,20,e);CHECK(v==((uint64_t)70<<32|1));
    /* Negative radius words: r + R = -3, d = 10000h >= -3 signed, miss. */
    setup();w32(0,MATRIX+0x14,0x10000);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,0);w16(0,VA(0x4a9814),0xffff);
    w16(0,SHAPES+8,0);w16(0,SHAPES+10,0);w16(0,SHAPES+12,0);
    v=es2_col_bound_registers(&memory,&g,SHAPES+8,0xfffe,e);CHECK(v==((uint64_t)0xfffffffdu<<32|0));
    /* A negative distance (x = 80000000h) is below every bound: AX = 1 with
     * the distance's high word. */
    setup();w32(0,MATRIX+0x14,0x80000000u);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,0);
    w16(0,SHAPES+8,0);w16(0,SHAPES+10,0);w16(0,SHAPES+12,0);
    v=es2_col_bound_registers(&memory,&g,SHAPES+8,20,e);CHECK(v==((uint64_t)70<<32|0x80000001u));
    /* JBE is unsigned: a negative local Y is above every bound -> miss
     * without the distance call; EDX stays 4801d4's. */
    setup();w32(0,MATRIX+0x18,(uint32_t)-5);w16(0,SHAPES+8,0);w16(0,SHAPES+10,0);w16(0,SHAPES+12,0);
    v=es2_col_bound_registers(&memory,&g,SHAPES+8,20,e);CHECK(v==((uint64_t)0xd1<<32|0)&&ncalls==1);
    /* Bound equal to Y: b = 20 + 1000 = 1020 = Y -> miss; Y = 1019 -> test. */
    setup();w32(0,MATRIX+0x18,1020);w32(0,MATRIX+0x14,0);w32(0,MATRIX+0x1c,0);w16(0,SHAPES+8,0);w16(0,SHAPES+10,0);w16(0,SHAPES+12,0);
    v=es2_col_bound_registers(&memory,&g,SHAPES+8,20,e);CHECK((uint32_t)v==0&&ncalls==1);
    setup();w32(0,MATRIX+0x18,1019);w32(0,MATRIX+0x14,0);w32(0,MATRIX+0x1c,0);w16(0,SHAPES+8,0);w16(0,SHAPES+10,0);w16(0,SHAPES+12,0);
    v=es2_col_bound_registers(&memory,&g,SHAPES+8,20,e);CHECK(v==((uint64_t)70<<32|1)&&ncalls==2);
    /* col_shape: radius word replaces AX of the entry EAX; a bound miss
     * returns 0 with col_bound's EDX; a hit calls sphere_hits. */
    setup();w32(0,MATRIX+0x14,0);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,0);shape(0,3,5000,0,0,10);
    v=es2_col_shape_registers(&memory,&g,SHAPES,e);
    CHECK(v==((uint64_t)60<<32|0)&&ncalls==3&&calls[0]==VA(0x40c4c4)&&args[0][0]==SHAPES+8&&args[0][1]==0x1111000au);
    CHECK(regs[0][0]==0x1111000au&&regs[0][1]==SHAPES+8&&regs[0][2]==0x55556666u);
    setup();w32(0,MATRIX+0x14,0);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,0);shape(0,3,5,0,0,10);sphere_result=777;
    v=es2_col_shape_registers(&memory,&g,SHAPES,e);
    CHECK(v==((uint64_t)0xf1<<32|777)&&ncalls==4&&calls[3]==VA(0x40c524)&&args[3][0]==SHAPES);
    CHECK(regs[3][0]==1&&regs[3][1]==60&&regs[3][2]==0xc3);
    /* col_group, part < 0: matrix copied (20 bytes, then the translation),
     * 4a980c = scratch, two shapes: region 0 flagged and hit, region 1 not
     * flagged. */
    setup();w32(0,MATRIX+0x14,0);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,0);
    for(unsigned i=0;i<20;++i)arena[MATRIX-BASE+i]=(uint8_t)i;
    w16(0,GROUPS,0xffff);w16(0,GROUPS+4,2);w32(0,GROUPS+6,SHAPES);shape(0,0,5,0,0,10);shape(1,1,5,0,0,10);
    w16(0,FLAGS,1);w16(0,FLAGS+2,0);sphere_result=555;w32(0,VA(0x4a9818),0);w16(0,VA(0x4a981c),0xffff);
    v=es2_col_group_registers(&memory,&g,GROUPS,ACTOR,MATRIX,FLAGS,e);
    CHECK(v==((uint64_t)FLAGS<<32|0));
    CHECK(r32(0,VA(0x4a980c))==SCRATCH&&!memcmp(arena+(SCRATCH-BASE),arena+(MATRIX-BASE),32));
    CHECK(r32(0,VA(0x4a9818))==555&&r16(0,VA(0x4a981c))==0);
    CHECK(calls[0]==VA(0x40c8c8)&&args[0][0]==SHAPES&&regs[0][0]==0&&regs[0][1]==FLAGS&&regs[0][2]==GROUPS);
    /* The last shape flagged but missed: EDX = col_shape's (col_bound's). */
    setup();w32(0,MATRIX+0x14,0);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,0);
    w16(0,GROUPS,0x8000);w16(0,GROUPS+4,1);w32(0,GROUPS+6,SHAPES);shape(0,0xffff,5000,0,0,10);w16(0,FLAGS-2,7);
    v=es2_col_group_registers(&memory,&g,GROUPS,ACTOR,MATRIX,FLAGS,e);CHECK(v==((uint64_t)60<<32|0));
    /* No shapes: EDX = G. */
    setup();w16(0,GROUPS,0xffff);w16(0,GROUPS+4,0);
    v=es2_col_group_registers(&memory,&g,GROUPS,ACTOR,MATRIX,FLAGS,e);CHECK(v==((uint64_t)GROUPS<<32|0)&&ncalls==0);
    /* part >= 0: lookup with EAX = L, EDX = vtable, entry ECX; node null,
     * node index -1, node index 2. */
    for(unsigned c=0;c<3;++c){
        setup();w16(0,GROUPS,7);w16(0,GROUPS+4,0);
        node_result=c==0?0:NODE;w16(0,NODE+4,c==1?0xffff:2);
        v=es2_col_group_registers(&memory,&g,GROUPS,ACTOR,MATRIX,FLAGS,e);
        uint32_t want=c==2?NODES+64:VA(0x6c56a8);
        CHECK(ncalls==2&&calls[0]==V20&&args[0][0]==LOOKUP&&args[0][1]==((GROUPS&0xffff0000u)|7));
        CHECK(regs[0][0]==LOOKUP&&regs[0][1]==VTABLE&&regs[0][2]==0x55556666u);
        CHECK(calls[1]==VA(0x47fa20)&&args[1][0]==want&&args[1][1]==MATRIX&&args[1][2]==SCRATCH);
        CHECK(regs[1][0]==want&&regs[1][1]==SCRATCH&&regs[1][2]==want&&v==((uint64_t)GROUPS<<32|0));
        CHECK(arena[SCRATCH-BASE+31]==0x5f&&r32(0,VA(0x4a980c))==SCRATCH);
    }
    /* col_query: two groups, the second hits; result pointer 4a981e. */
    setup();w32(0,MATRIX+0x14,0);w32(0,MATRIX+0x18,0);w32(0,MATRIX+0x1c,0);
    w16(0,COL,2);w32(0,COL+2,GROUPS);w16(0,GROUPS,0xffff);w16(0,GROUPS+4,0);
    w16(0,GROUPS+10,0xffff);w16(0,GROUPS+14,1);w32(0,GROUPS+16,SHAPES);shape(0,0,5,0,0,10);w16(0,FLAGS,1);
    w32(0,SEGMENT+4,1000);w32(0,SEGMENT+8,0x12340032u);sphere_result=444;
    w32(0,VA(0x4a9818),9);w16(0,VA(0x4a981c),9);
    v=es2_col_query_registers(&memory,&g,COL,ACTOR,SEGMENT,MATRIX,FLAGS,e);
    CHECK((uint32_t)v==VA(0x4a981e)&&(uint32_t)(v>>32)==(444u&0xffff0000u));
    CHECK(r32(0,VA(0x4a981e))==444&&r16(0,VA(0x4a9822))==0&&r32(0,VA(0x4a9810))==1000&&r16(0,VA(0x4a9814))==0x32);
    CHECK(calls[0]==VA(0x40c8fc)&&args[0][0]==GROUPS&&args[0][1]==ACTOR&&args[0][2]==MATRIX&&args[0][3]==FLAGS);
    CHECK(regs[0][0]==FLAGS&&regs[0][1]==MATRIX&&regs[0][2]==ACTOR);
    /* No group: EAX 0, EDX = lo([S+4], FFFFh), the region word -1. */
    setup();w16(0,COL,0);w32(0,COL+2,GROUPS);w32(0,SEGMENT+4,0x56781234u);w16(0,SEGMENT+8,7);
    v=es2_col_query_registers(&memory,&g,COL,ACTOR,SEGMENT,MATRIX,FLAGS,e);
    CHECK(v==((uint64_t)0x5678ffffu<<32|0)&&r32(0,VA(0x4a981e))==0&&r16(0,VA(0x4a9822))==0xffff&&ncalls==0);
    /* A negative count is no group. */
    setup();w16(0,COL,0x8001);w32(0,COL+2,GROUPS);w32(0,SEGMENT+4,0);
    v=es2_col_query_registers(&memory,&g,COL,ACTOR,SEGMENT,MATRIX,FLAGS,e);CHECK(ncalls==0&&(uint32_t)v==0);
    printf("col: %u checks\n",checks);
    return 0;
}
