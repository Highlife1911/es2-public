#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/meteor.h"
/* Synthetic guest: an arena at 90000000h (object, point, shape, tables,
 * counter, blocks), a frame scratch at A0000000h, an image base I with
 * the globals 0x6cbee4 (counter pointer), 0x4a96b4 (resource table) and
 * 0x6c5830 (arena descriptor). Every game call is logged with its
 * arguments, entry registers, the context word f-14h and the buffer
 * contents it reads; the stubs answer from a small plan. Expectations
 * from the reference listing (main_loop.md, section "meteor constructor 409b44
 * and shape instance 490b64"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_METEOR_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_METEOR_SCRATCH],globals[3][4];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"meteor line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x6cbee4)<4)return globals[0]+(a-G(0x6cbee4));
    if(a-G(0x4a96b4)<4)return globals[1]+(a-G(0x4a96b4));
    if(a-G(0x6c5830)<4)return globals[2]+(a-G(0x6c5830));
    fprintf(stderr,"meteor: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define O (OBJ+0x100)
#define P (OBJ+0x200)
#define TABLE (OBJ+0x220)
#define SHAPE1 (OBJ+0x240)
#define SHAPE2 (OBJ+0x280)
#define SOURCE (OBJ+0x2c0)
#define COUNTER (OBJ+0x300)
#define ARENA_D (OBJ+0x310)
#define BLOCK (OBJ+0x400)
#define HEAP (OBJ+0x500)
#define HEAD UINT32_C(0x0012ff00)
typedef struct logged { uint32_t f,a[3],r[3],context,input[4]; unsigned n; } logged;
static logged log_[32];static unsigned calls;
static uint32_t bounded,heap_result,draw;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls++;memset(l,0,sizeof(*l));l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f!=G(0x49630c))l->context=r16(0,F(0x14));
    uint32_t eax=0xe0e00000u+calls,edx=0xd0d00000u+calls,ecx=0xc0c00000u+calls;
    if(f==G(0x49630c)){CHECK(n==0&&r->eax==G(0x498170));w32(0,F(0x24),HEAD);w32(0,F(0x14),0);w32(0,F(0x10),0);eax=HEAD;edx=r->edx;ecx=r->ecx;}
    else if(f==G(0x402188)){CHECK(n==2);eax=a[0];}
    else if(f==G(0x492ee0)){CHECK(n==1&&a[0]==G(0x4d2599));eax=draw;}
    else if(f==G(0x47eb30)){CHECK(n==2&&a[1]==F(0x2c));w16(0,a[1],0x4000);w16(0,a[1]+2,0x1000);w16(0,a[1]+4,0xf000);w16(0,a[1]+6,0x4000);}
    else if(f==G(0x492f24)){CHECK(n==2&&a[0]==G(0x4d2599)&&a[1]==25000);eax=bounded;}
    else if(f==G(0x47ffe8)){CHECK(n==3);l->input[0]=r32(0,a[0]);l->input[1]=r32(0,a[0]+4);w32(0,a[2],100);w32(0,a[2]+4,200);}
    else if(f==G(0x47ff8c)){CHECK(n==3);l->input[0]=r16(0,a[0]);l->input[1]=r16(0,a[0]+2);l->input[2]=r16(0,a[0]+4);l->input[3]=r32(0,F(0x40));
        w16(0,a[2],0x1234);w16(0,a[2]+2,0x5678);}
    else if(f==G(0x47749c)){CHECK(n==1&&a[0]==12);eax=heap_result;}
    else if(f==G(0x490b64)){CHECK(n==2);eax=a[0];edx=COUNTER;}
    else if(f==G(0x47a288)){CHECK(n==2);eax=BLOCK;}
    else if(f==G(0x49680e)){CHECK(n==3);memcpy(host(0,a[0]),host(0,a[1]),a[2]);eax=a[0];}
    else {fprintf(stderr,"meteor: unexpected call %08x\n",f);exit(1);}
    r->eax=eax;r->edx=edx;r->ecx=ecx;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0x5a,sizeof(arena));memset(scratch,0x5a,sizeof(scratch));calls=0;
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,100);w32(0,G(0x4a96b4),TABLE);w32(0,G(0x6c5830),ARENA_D);
    w32(0,TABLE,SHAPE1);w32(0,TABLE+4,SHAPE2);
    w32(0,P,1000);w32(0,P+4,2000);w32(0,P+8,3000);
}
static void shape_checks(void){
    es2_weapon_graph g=graph();reset();uint32_t ecx=0;
    w16(0,SHAPE2+0x24,3);w32(0,SHAPE2+0x1c,SOURCE);for(unsigned i=0;i<6;++i)arena[SOURCE-OBJ+i]=(uint8_t)(0x41+i);
    uint64_t v=es2_shape_instance_registers(&memory,&g,O,SHAPE2,entry,&ecx);
    CHECK(v==((uint64_t)COUNTER<<32|O)&&r32(0,COUNTER)==102&&r32(0,O)==G(0x4a62ed)&&r32(0,O+4)==SHAPE2&&r32(0,O+8)==BLOCK);
    CHECK(calls==2&&log_[0].f==G(0x47a288)&&log_[0].a[0]==6&&log_[0].a[1]==ARENA_D&&log_[0].r[0]==ARENA_D&&log_[0].r[1]==6&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x49680e)&&log_[1].a[0]==BLOCK&&log_[1].a[1]==SOURCE&&log_[1].a[2]==6&&log_[1].r[0]==SHAPE2&&log_[1].r[1]==6&&log_[1].r[2]==SOURCE);
    CHECK(!memcmp(arena+(BLOCK-OBJ),"ABCDEF",6)&&ecx==0xc0c00002u);
    /* count 0: no call, O+8 = 0, ECX = the entry ECX */
    reset();w16(0,SHAPE2+0x24,0);
    v=es2_shape_instance_registers(&memory,&g,O,SHAPE2,entry,&ecx);
    CHECK(calls==0&&v==((uint64_t)COUNTER<<32|O)&&r32(0,O+8)==0&&ecx==entry[2]&&r32(0,COUNTER)==102);
    /* S = O: the vtable and O+4 stores land in the shape before its count
       and source are read (S+24h, S+1Ch stay intact) */
    reset();w32(0,O+0x1c,SOURCE);w16(0,O+0x24,2);
    v=es2_shape_instance_registers(&memory,&g,O,O,entry,&ecx);
    CHECK(calls==2&&log_[0].a[0]==4&&log_[1].a[1]==SOURCE&&log_[1].a[2]==4&&r32(0,O+4)==O);
}
/* meteor_create with a plan: the draw, rng_bounded's AX, the heap result. */
static void meteor_case(uint32_t d,uint32_t b,uint32_t h,int32_t height,int32_t drop){
    es2_weapon_graph g=graph();reset();uint32_t ecx=0;draw=d;bounded=b;heap_result=h;
    uint64_t v=es2_meteor_create_registers(&memory,&g,O,P,0xabcd0001u,entry,&ecx);
    const unsigned k=h?9:8;
    CHECK(calls==k&&v==((uint64_t)COUNTER<<32|O)&&ecx==HEAD);
    CHECK(log_[0].f==G(0x49630c)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x402188)&&log_[1].a[0]==O&&log_[1].a[1]==SHAPE1&&log_[1].r[0]==HEAD&&log_[1].r[1]==TABLE&&log_[1].r[2]==SHAPE1&&log_[1].context==0);
    CHECK(log_[2].f==G(0x492ee0)&&log_[2].r[0]==O+0x45&&log_[2].r[1]==0xd0d00002u&&log_[2].r[2]==0xc0c00002u);
    CHECK(log_[3].f==G(0x47eb30)&&log_[3].a[0]==d&&log_[3].r[0]==d&&log_[3].r[1]==F(0x2c)&&log_[3].r[2]==G(0x4d2599));
    CHECK(log_[4].f==G(0x492f24)&&log_[4].r[0]==0xe0e00004u&&log_[4].r[1]==0xd0d00004u&&log_[4].r[2]==0xc0c00004u);
    const int32_t s=0x11170+(int16_t)(uint16_t)b;
    CHECK(log_[5].f==G(0x47ffe8)&&log_[5].a[0]==F(0x34)&&log_[5].a[1]==F(0x2c)&&log_[5].a[2]==F(0x48)&&log_[5].input[0]==0&&log_[5].input[1]==(uint32_t)-s);
    CHECK(log_[5].r[0]==F(0x2c)&&log_[5].r[1]==F(0x34)&&log_[5].r[2]==F(0x48));
    CHECK(log_[6].f==G(0x47ff8c)&&log_[6].a[0]==F(0x3c)&&log_[6].a[1]==F(0x2c)&&log_[6].a[2]==O+0x45&&log_[6].r[0]==O+0x45&&log_[6].r[1]==F(0x2c)&&log_[6].r[2]==F(0x3c));
    CHECK(log_[6].input[0]==0&&log_[6].input[1]==2000&&log_[6].input[2]==0&&log_[6].input[3]==(uint32_t)height);
    CHECK(log_[7].f==G(0x47749c)&&log_[7].r[0]==O+0x26&&log_[7].r[1]==2200&&log_[7].r[2]==1100);
    CHECK(r32(0,O)==G(0x4982b8)&&r32(0,O+0x55)==0xabcd0001u&&r16(0,O+0x45)==0x1234&&r16(0,O+0x47)==0x5678);
    CHECK(r16(0,O+0x49)==(uint16_t)drop&&r16(0,O+0x10)==(uint16_t)d&&r16(0,O+0x32)==0&&r32(0,O+0x4d)==0&&r32(0,O+0x51)==0);
    CHECK(r32(0,O+0x26)==1100&&r32(0,O+0x2a)==2200&&r32(0,O+0x2e)==(uint32_t)height+3000&&arena[O-OBJ+0x4b]==0&&arena[O-OBJ+0x4c]==0);
    CHECK(r32(0,O+0x41)==h&&r32(0,F(4))==h);
    if(h){
        CHECK(log_[8].f==G(0x490b64)&&log_[8].a[0]==h&&log_[8].a[1]==SHAPE2&&log_[8].r[0]==SHAPE2&&log_[8].r[1]==h&&log_[8].r[2]==TABLE&&log_[8].context==0x14);
        CHECK(r32(0,COUNTER)==99&&r16(0,F(0x14))==8);
    } else CHECK(r32(0,COUNTER)==101&&r16(0,F(0x14))==0);
}
int main(void){
    shape_checks();
    /* b = 5000: s = 75000, q = 37, z = 50*(1369>>1) = 34200, drop = -924 (truncated) */
    meteor_case(0x12345678u,0xffff1388u,HEAP,34200,-924);
    /* b = 8000h: s = 37232, q = 18, z = 50*162 = 8100, drop = -450; heap fails */
    meteor_case(0x0000beefu,0x00008000u,0,8100,-450);
    /* b = 7FFFh: s = 102767, q = 51, z = 50*1300 = 65000, drop = -1274 (-1274.5 truncated) */
    meteor_case(0xffff0001u,0x7fffu,HEAP,65000,-1274);
    printf("PASS: meteor %u checks\n",checks);return 0;
}
