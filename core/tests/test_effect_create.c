#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/effect_create.h"
/* Synthetic guest for the effect creation: objects at 90000000h, an
 * image base B with the globals the four functions read. Every game call is
 * logged with its arguments and entry registers. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[8192],globals1[0x200],globals2[0x10];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"effect_create line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x4a9500)<sizeof(globals1))return globals1+(a-G(0x4a9500));
    if(a-G(0x6cbee0)<sizeof(globals2))return globals2+(a-G(0x6cbee0));
    fprintf(stderr,"effect_create: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; } logged;
static logged log_[32];static unsigned calls;static uint32_t heap_result,shape_for_object;
#define COUNTER (OBJ+0x10)
#define SCRATCH (OBJ+0x1000)
#define FRAME (SCRATCH+208)
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49630c)){CHECK(n==0);r->eax=0x5eedu;}
    else if(f==G(0x47749c)){CHECK(n==1&&a[0]==12);r->eax=heap_result;r->edx=0x47;r->ecx=0x49;}
    else if(f==G(0x490b64)){CHECK(n==2);r->eax=a[0];r->edx=0x490;r->ecx=0xb64;}
    else if(f==G(0x49681a)){CHECK(n==3&&a[1]==0&&a[2]==8);memset(host(0,a[0]),0,8);r->eax=a[0];r->edx=0x3e3e;r->ecx=0x5e5e;}
    else if(f==G(0x406f38)){CHECK(n==2);r->eax=0xabcd0003u;r->edx=0x6f;r->ecx=0x38;}
    else if(f==G(0x407048)){CHECK(n==3);r->eax=0x70;r->edx=0x7048;r->ecx=0x48;}
    else if(f==G(0x471b30)){CHECK(n==1);uint64_t p=es2_pool_alloc_registers(&memory,a[0],(const uint32_t[3]){r->eax,r->edx,r->ecx});
        r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);r->ecx=r->eax;}
    else if(f==G(0x402188)){CHECK(n==2);w32(0,a[0]+0x34,shape_for_object);r->eax=a[0];r->edx=0x2188;r->ecx=0x21;}
    else if(f==G(0x407604)){CHECK(n==3);r->eax=a[0];r->edx=0x7604;r->ecx=0x76;}
    else if(f==G(0x492f24)){CHECK(n==2&&a[0]==G(0x4d260b)&&a[1]==50);r->eax=0x1f;r->edx=0x2f;r->ecx=0x3f;}
    else if(f==G(0x4628e0)){CHECK(n==2);r->edx=0x4628;}
    else CHECK(0);
}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,SCRATCH,0,0,0,call,0,0,0};return g;}
static int is(unsigned i,uint32_t va){return log_[i].f==G(va);}
/* Pool of three 0x20-byte objects: H at OBJ+0x100, nodes at OBJ+0x200.. */
static uint32_t pool_setup(uint32_t h,uint32_t nodes,unsigned count){
    w16(0,h,(uint16_t)count);w16(0,h+2,0);w32(0,h+0xe,0);
    for(unsigned i=0;i<count;++i){uint32_t n=nodes+0x28u*i;w32(0,n,i+1<count?n+0x28u:0);w32(0,n+4,i?n-0x28u:0);}
    w32(0,h+0xa,count?nodes:0);return h;
}
static void pool_checks(void){
    uint32_t h=pool_setup(OBJ+0x100,OBJ+0x200,3),e[3]={1,0x77,3},got[3];
    for(unsigned i=0;i<3;++i){uint64_t p=es2_pool_alloc_registers(&memory,h,e);got[i]=(uint32_t)p;
        CHECK(got[i]==OBJ+0x200+0x28u*i+8u&&(uint32_t)(p>>32)==got[i]-8u);
        CHECK(r16(0,h)==2-i&&r16(0,h+2)==i+1&&r32(0,h+0xe)==got[i]-8u&&r32(0,got[i]-4u)==0);}
    /* Newest first: head -> 3 -> 2 -> 1, previous links point back. */
    CHECK(r32(0,got[2]-8u)==got[1]-8u&&r32(0,got[1]-8u)==got[0]-8u&&r32(0,got[0]-8u)==0);
    CHECK(r32(0,got[0]-4u)==got[1]-8u&&r32(0,got[1]-4u)==got[2]-8u);
    CHECK(r32(0,h+0xa)==0);
    uint64_t p=es2_pool_alloc_registers(&memory,h,e);CHECK(p==(uint64_t)0x77<<32);
}
static void object_checks(void){
    es2_weapon_graph g=graph();uint32_t o=OBJ+0x400,e[3]={0xe0,0xe1,0xe2};
    memset(arena,0xcc,sizeof(arena));w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,100);
    calls=0;uint64_t p=es2_object_init_registers(&memory,&g,o,0,e);
    CHECK(p==((uint64_t)0x3e3e<<32|o)&&calls==2&&is(0,0x49630c)&&is(1,0x49681a));
    CHECK(log_[0].r[0]==G(0x497115)&&log_[0].r[1]==0xe1&&log_[0].r[2]==0xe2);
    CHECK(log_[1].a[0]==o+0x38&&log_[1].r[0]==o+0x38&&log_[1].r[1]==0&&log_[1].r[2]==0);
    CHECK(r32(0,o)==G(0x4a0b80)&&r32(0,o+0x34)==0&&r32(0,COUNTER)==103&&r32(0,o+0x26)==0&&r16(0,o+0x32)==0&&r32(0,o+0xc)==0);
    /* Allocation fails: ECX = 12 at memset. */
    heap_result=0;calls=0;p=es2_object_init_registers(&memory,&g,o,0x1234,e);
    CHECK(calls==3&&is(1,0x47749c)&&log_[1].r[0]==COUNTER&&log_[2].r[1]==0&&log_[2].r[2]==12&&r32(0,FRAME-4)==0);
    /* Allocation succeeds: the shape constructor and the context words. */
    heap_result=OBJ+0x800;calls=0;w32(0,COUNTER,0);w16(0,FRAME-0x14,0xffff);
    p=es2_object_init_registers(&memory,&g,o,0x1234,e);
    CHECK(calls==4&&is(2,0x490b64)&&log_[2].a[0]==OBJ+0x800&&log_[2].a[1]==0x1234&&log_[2].r[1]==0x1234&&log_[2].r[2]==OBJ+0x800);
    CHECK(log_[3].r[1]==OBJ+0x800&&log_[3].r[2]==0xb64&&r32(0,o+0x34)==OBJ+0x800&&r32(0,COUNTER)==1&&r16(0,FRAME-0x14)==8&&r32(0,FRAME-4)==OBJ+0x800);
}
static void light_checks(void){
    es2_weapon_graph g=graph();uint32_t l=OBJ+0x480;
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,5);w32(0,G(0x4a9608),OBJ+0x900);
    calls=0;uint64_t p=es2_light_create_registers(&memory,&g,l,OBJ+0x60,0x12345678u);
    CHECK(p==((uint64_t)0x7048<<32|l)&&calls==2&&is(0,0x406f38)&&is(1,0x407048));
    CHECK(log_[0].a[0]==OBJ+0x900&&log_[0].a[1]==OBJ+0x60&&log_[0].r[0]==COUNTER&&log_[0].r[1]==OBJ+0x60&&log_[0].r[2]==OBJ+0x900);
    CHECK(log_[1].a[1]==0xabcd0003u&&log_[1].a[2]==0xabcd5678u&&log_[1].r[2]==0x38&&r16(0,l+0xc)==3&&r32(0,l)==G(0x497b8a)&&r32(0,COUNTER)==7);
}
static void explosion_checks(void){
    es2_weapon_graph g=graph();uint32_t o=OBJ+0x300,types=OBJ+0xa00,p=OBJ+0x60,e[3]={1,2,3};
    memset(arena,0,sizeof(arena));w32(0,G(0x6cbee4),COUNTER);
    w32(0,G(0x4a9628),types+40);w32(0,G(0x4a9630),OBJ+0xc00);w32(0,G(0x4a963c),OBJ+0xc40);
    w32(0,G(0x4a968d),pool_setup(OBJ+0x100,OBJ+0x200,1));w32(0,G(0x4a95fe),pool_setup(OBJ+0x140,OBJ+0x280,1));
    w32(0,G(0x4a9698),OBJ+0xc80);w32(0,OBJ+0xc84,0x5a5a);
    /* Type -1 (16-bit 0xffff): entry types+0; its animation word 2. */
    uint32_t t=types;w16(0,t,2);w16(0,t+2,0x77);w16(0,t+4,1);w16(0,t+6,1);w16(0,t+8,0x99);w16(0,t+0x24,5);w16(0,t+0x26,0);
    w32(0,OBJ+0xc00+4*2,0xbeef);w16(0,OBJ+0xc40+4*2,3);
    shape_for_object=OBJ+0xd00;w32(0,OBJ+0xd08,OBJ+0xd40);w16(0,OBJ+0xd40+6,0x1111);
    w32(0,p,10);w32(0,p+4,20);w32(0,p+8,30);
    calls=0;uint64_t r=es2_explosion_create_registers(&memory,&g,o,0x1234ffffu,p,0xdddd,1,e);
    CHECK(r==((uint64_t)COUNTER<<32|o));
    CHECK(calls==8&&is(0,0x49630c)&&is(1,0x402188)&&is(2,0x471b30)&&is(3,0x402188)&&is(4,0x471b30)&&is(5,0x407604)&&is(6,0x492f24)&&is(7,0x4628e0));
    CHECK(log_[1].a[1]==0xbeef&&log_[1].r[0]==0xbeef&&log_[1].r[1]==2&&log_[1].r[2]==OBJ+0xc00);
    CHECK(r32(0,o)==G(0x497d4c)&&r32(0,o+0x57)==0xdddd&&arena[0x341]==0xff&&r16(0,o+4)==2&&r16(0,o+0x4b)==0x77&&r16(0,OBJ+0xd46)==0);
    CHECK(r32(0,o+0x26)==10&&r32(0,o+0x2a)==20&&r32(0,o+0x2e)==30);
    CHECK(log_[2].r[0]==OBJ+0x100&&log_[2].r[1]==20&&log_[2].r[2]==10);
    uint32_t k=OBJ+0x208,l=OBJ+0x288;
    CHECK(log_[3].a[0]==k&&log_[3].a[1]==0x5a5a&&log_[3].r[0]==OBJ+0xc80&&log_[3].r[2]==k);
    CHECK(r32(0,k)==G(0x49a262)&&r16(0,k+4)==9&&r32(0,o+0x4f)==k&&r32(0,k+0x26)==10&&r32(0,k+0x2e)==30);
    CHECK(log_[4].r[0]==OBJ+0x140&&log_[4].r[1]==20&&log_[4].r[2]==10);
    CHECK(log_[5].a[0]==l&&log_[5].a[1]==p&&log_[5].a[2]==((l&0xffff0000u)|0x99)&&log_[5].r[2]==l&&r32(0,o+0x53)==l);
    CHECK(log_[6].r[0]==types&&log_[6].r[2]==o&&log_[7].a[0]==15&&log_[7].a[1]==p&&log_[7].r[0]==p&&log_[7].r[1]==0x2f);
    CHECK(r32(0,COUNTER)==(uint32_t)(1-4-2+1)&&r16(0,FRAME-0x18)==0x20&&r32(0,FRAME-4)==k&&r32(0,FRAME-8)==l);
    /* Empty light pool, no linked object, no sound: [O+53] = 0. */
    w16(0,t+4,0);w16(0,t+0x26,1);w16(0,OBJ+0xc40+4*2,0xffff);
    calls=0;r=es2_explosion_create_registers(&memory,&g,o,0xffff,p,0,0x100,e);
    CHECK(calls==3&&is(2,0x471b30)&&r32(0,o+0x4f)==0&&r32(0,o+0x53)==0&&r16(0,o+4)==8);
    CHECK(log_[2].r[0]==OBJ+0x140&&log_[2].r[1]==20&&log_[2].r[2]==10);
}
int main(void){
    pool_checks();object_checks();light_checks();explosion_checks();
    printf("effect_create core: %u checks passed\n",checks);return 0;
}
