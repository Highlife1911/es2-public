#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/stage2_types.h"
/* Synthetic state for the core (the stage-2 callbacks with type data
 * and the weapon-list entry). The image base is placed so that the globals
 * 498514..4a9e00 and 6cbee4 fall inside the arena. A logging call function
 * stands in for every game call (target, arguments, count, entry
 * EAX/EDX/ECX): new[] and the array construction 49667c return the next
 * block of a bump area, memset fills, stream_read 4735b8 and the read method
 * copy from a scripted byte stream, the shape loader 474cd8 stores a list of
 * shapes and returns its count, and every other call returns EAX = A0h+n,
 * EDX = D0h+n, ECX = C0h+n (n = call number). Expected values are worked
 * out by hand from the contract in mission_load.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x98000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define HEAP (BASE+0x300000u)
#define OBJ (BASE+0x2f0000u)
#define SHAPES (OBJ+0x3000u)
#define READ_METHOD UINT32_C(0x77700000)
static uint8_t arena[0x310000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"stage2_types line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct call { uint32_t va,arg[6],count,eax,edx,ecx; } call;
static call calls[128];static unsigned call_count,observed,slot_seen;
static uint32_t heap_next,shape_count;
static const uint8_t *stream_bytes;static unsigned stream_at;
static uint32_t va_of(uint32_t f){return f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<128);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=va_of(f);for(unsigned i=0;i<n&&i<6;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    const uint32_t eax=0xa0u+call_count,edx=0xd0u+call_count,ecx=0xc0u+call_count;
    r->eax=eax;r->edx=edx;r->ecx=ecx;
    if(k->va==0x4774f0u){r->eax=heap_next;heap_next+=(a[0]+7u)&~3u;}
    else if(k->va==0x49667cu){r->eax=heap_next;heap_next+=(a[1]*a[2]+7u)&~3u;}
    else if(k->va==0x49681au){for(uint32_t i=0;i<a[2];++i)((uint8_t *)host(0,a[0]+i))[0]=(uint8_t)a[1];r->eax=a[0];}
    else if(f==READ_METHOD||k->va==0x4735b8u){
        for(uint32_t i=0;i<a[1]&&a[1]<0x1000u;++i)((uint8_t *)host(0,a[2]+i))[0]=stream_bytes[stream_at++];
        r->eax=0x101u;
    } else if(k->va==0x474cd8u){
        w32(0,a[0],SHAPES);
        for(uint32_t i=0;i<4;++i)w32(0,SHAPES+4u*i,SHAPES+0x40u+0x40u*i);
        r->eax=0x70000u|shape_count;
    } else if(k->va==0x49630cu){r->eax=0x5555u;r->edx=k->edx;r->ecx=k->ecx;}   /* EDX/ECX kept */
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;if(slot)++slot_seen;++observed;}
static es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
#define SCRATCH (OBJ+0x1000u)
#define F (SCRATCH+ES2_STAGE2_TYPES_FRAME)
#define S (F-0x30u)
#define TABLE (OBJ+0x200u)
static void setup(void){
    memset(arena,0xcc,sizeof(arena));call_count=observed=slot_seen=0;heap_next=HEAP;stream_at=0;shape_count=0;
    graph.scratch=SCRATCH;
    w32(0,F-0x2cu,0);w32(0,F-0x50u,0x1111u);w32(0,S,TABLE);w32(0,TABLE+0x18u,READ_METHOD);
    w32(0,VA(0x4a0a10u),0x7100u);w32(0,VA(0x4a09f8u),0x7200u);w32(0,VA(0x4a09f0u),0x7300u);
    w32(0,VA(0x6cbee4u),OBJ+0x80u);w32(0,OBJ+0x80u,41);
}
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}
/* The six callbacks open their file the same way: 472f14(S, name) with EAX
 * = S and EDX/ECX the path builder's. */
static void check_open(unsigned i,uint32_t name){
    CHECK(calls[i].va==0x472f14u&&calls[i].count==2&&calls[i].arg[0]==S&&calls[i].arg[1]==name&&calls[i].eax==S);
    CHECK(r16(0,F-0x40u)==8);
}

static void test_weapon_entry(void){
    uint32_t ecx=0;
    setup();
    const uint32_t E=OBJ+0x400u;w16(0,E+0x48u,0x7777u);
    static const uint8_t bytes[0x30]={1};stream_bytes=bytes;
    uint64_t p=es2_weapon_entry_registers(&memory,&graph,E,S,entry,&ecx);
    CHECK(call_count==3&&observed==3&&slot_seen==1);
    CHECK(calls[0].va==0x40cff8u&&calls[0].count==2&&calls[0].arg[0]==E&&calls[0].arg[1]==S&&calls[0].eax==0xe0&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x40cc14u&&calls[1].arg[0]==E+0x12u&&calls[1].arg[1]==S&&calls[1].eax==E+0x12u&&calls[1].edx==0xd1u&&calls[1].ecx==0xc1u);
    CHECK(calls[2].va==READ_METHOD&&calls[2].arg[0]==S&&calls[2].arg[1]==0x30u&&calls[2].arg[2]==E+0x22u&&
        calls[2].eax==E+0x22u&&calls[2].edx==0x30u&&calls[2].ecx==TABLE);
    /* EAX = the read's AL (101h -> 1), EDX/ECX the read's; word +48h (inside
     * the read data) cleared after the read. */
    CHECK(lo(p)==1&&hi(p)==0xd3u&&ecx==0xc3u&&r16(0,E+0x48u)==0&&arena[E+0x22u-BASE]==1);
}

static void test_rocket_types(void){
    uint32_t ecx=0;
    setup();
    /* three records whose words +0 are 5, -2, 9: max(0, 6, -1, 10) = 10 */
    static const uint8_t bytes[]={3,0, 5,0,0,0,0,0,0,0,0,0,0,0,0,0, 0xfe,0xff,0,0,0,0,0,0,0,0,0,0,0,0, 9,0,0,0,0,0,0,0,0,0,0,0,0,0};
    stream_bytes=bytes;
    uint64_t p=es2_rocket_types_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==9&&observed==9&&slot_seen==0);
    CHECK(calls[0].va==0x49630cu&&calls[0].count==0&&calls[0].eax==VA(0x49834cu)&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x492becu&&calls[1].arg[0]==VA(0x498420u)&&calls[1].arg[1]==0x7300u&&calls[1].eax==0x5555u&&calls[1].edx==0x7300u);
    check_open(2,0xa2u);
    CHECK(calls[2].edx==0xd2u&&calls[2].ecx==0xc2u);
    CHECK(calls[3].va==0x4735b8u&&calls[3].arg[0]==S&&calls[3].arg[1]==2&&calls[3].arg[2]==VA(0x4a96d4u)&&calls[3].eax==0&&calls[3].ecx==S);
    CHECK(calls[4].va==0x4774f0u&&calls[4].arg[0]==42&&calls[4].eax==3&&calls[4].edx==42);
    CHECK(calls[5].va==0x4735b8u&&calls[5].arg[1]==42&&calls[5].arg[2]==HEAP&&calls[5].eax==3&&calls[5].edx==42&&calls[5].ecx==S);
    CHECK(calls[6].va==0x472f64u&&calls[6].arg[0]==S&&calls[6].arg[1]==2&&calls[6].eax==S&&calls[6].edx==0xd6u);
    /* after the loop: EDX = the records + 3 x 14, ECX = 3 */
    CHECK(calls[7].va==0x492becu&&calls[7].arg[0]==VA(0x498433u)&&calls[7].arg[1]==0x7200u&&calls[7].eax==0x7200u&&
        calls[7].edx==HEAP+42u&&calls[7].ecx==3);
    CHECK(r32(0,VA(0x4a96d0u))==HEAP&&r16(0,VA(0x4a96d4u))==3&&r16(0,VA(0x4a96dcu))==10);
    CHECK(lo(p)==0x1111u&&hi(p)==0xd9u&&ecx==0xc9u);
    /* A record word 7FFFh gives 8000h (negative): the maximum stays 0. */
    static const uint8_t wrap[]={1,0, 0xff,0x7f,0,0,0,0,0,0,0,0,0,0,0,0};
    setup();stream_bytes=wrap;
    (void)es2_rocket_types_registers(&memory,&graph,entry,&ecx);
    CHECK(r16(0,VA(0x4a96dcu))==0);
}

static void test_bullet_types(void){
    uint32_t ecx=0;
    setup();arena[VA(0x498514u)-BASE]=0;
    /* two records 3 and 0: max 4; two shapes */
    static const uint8_t bytes[]={2,0, 3,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    stream_bytes=bytes;shape_count=2;
    uint64_t p=es2_bullet_types_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==12);
    CHECK(arena[VA(0x498514u)-BASE]==1&&r16(0,VA(0x4a9704u))==4&&r32(0,VA(0x4a96f8u))==HEAP);
    /* the shapes 49860c after the loop: EAX = [4a09f8], EDX = sx(count) = 2,
     * ECX = the records + 2 x 14 */
    CHECK(calls[7].va==0x492becu&&calls[7].arg[0]==VA(0x49860cu)&&calls[7].eax==0x7200u&&calls[7].edx==2&&calls[7].ecx==HEAP+28u);
    CHECK(calls[8].va==0x474cd8u&&calls[8].count==4&&calls[8].arg[0]==VA(0x4a9700u)&&calls[8].arg[1]==0xa8u&&calls[8].arg[2]==0x140008u&&calls[8].arg[3]==0);
    CHECK(calls[9].va==0x492becu&&calls[9].arg[0]==VA(0x498614u)&&calls[9].arg[1]==0x7100u&&calls[9].eax==0x7100u&&calls[9].edx==0xd9u);
    CHECK(calls[10].va==0x45cdf4u&&calls[10].arg[0]==0xaau&&calls[10].eax==0xaau);
    CHECK(calls[11].va==0x46a03cu&&calls[11].arg[0]==0xabu&&calls[11].eax==0xabu&&calls[11].edx==0xdbu&&calls[11].ecx==0xaau);
    CHECK(r32(0,VA(0x4a9706u))==0xabu&&r32(0,VA(0x4a970au))==0xacu);
    CHECK(r32(0,SHAPES+0x40u+0x26u)==VA(0x4a9706u)&&r32(0,SHAPES+0x80u+0x26u)==VA(0x4a9706u)&&r32(0,SHAPES+0xc0u+0x26u)==0xccccccccu);
    /* EDX = sx(BX) = 2, ECX = the last shape */
    CHECK(lo(p)==0x1111u&&hi(p)==2&&ecx==SHAPES+0x80u);
    /* The flag stays as it is when set; no shape: ECX = the bitmap. */
    setup();arena[VA(0x498514u)-BASE]=7;stream_bytes=bytes;shape_count=0;
    p=es2_bullet_types_registers(&memory,&graph,entry,&ecx);
    CHECK(arena[VA(0x498514u)-BASE]==7&&hi(p)==0&&ecx==0xabu);
}

static void test_beam_types(void){
    uint32_t ecx=0;
    setup();
    static const uint16_t steps[4]={1,0,1,2};
    for(unsigned i=0;i<4;++i)w16(0,VA(0x498640u)+2*i,steps[i]);
    static const uint8_t bytes[]={2,0, 1,2,3,4,5,6,7,8,9,10,11,12};
    stream_bytes=bytes;
    uint64_t p=es2_beam_types_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==11&&slot_seen==1);
    CHECK(r16(0,VA(0x4a9712u))==1&&r16(0,VA(0x4a9714u))==0&&r16(0,VA(0x4a9716u))==2&&r16(0,VA(0x4a9718u))==3&&
        r16(0,VA(0x4a971au))==3&&r16(0,VA(0x4a9712u)+2*119)==61&&r16(0,VA(0x4a9712u)+2*120)==0xccccu);
    /* after the table: EAX = [4a09f0], EDX = 4a9802, ECX = 3 */
    CHECK(calls[1].va==0x492becu&&calls[1].arg[0]==VA(0x49877cu)&&calls[1].eax==0x7300u&&calls[1].edx==VA(0x4a9802u)&&calls[1].ecx==3);
    check_open(2,0xa2u);
    CHECK(calls[3].va==READ_METHOD&&calls[3].arg[0]==S&&calls[3].arg[1]==2&&calls[3].arg[2]==F-0x52u&&calls[3].eax==S&&
        calls[3].edx==TABLE&&calls[3].ecx==F-0x52u);
    CHECK(calls[4].va==0x4774f0u&&calls[4].arg[0]==12&&calls[4].eax==12&&calls[4].ecx==2);
    CHECK(calls[5].va==0x4735b8u&&calls[5].arg[1]==12&&calls[5].arg[2]==HEAP&&calls[5].eax==2&&calls[5].edx==12&&calls[5].ecx==S);
    CHECK(calls[6].va==0x472f64u&&calls[6].eax==S);
    CHECK(calls[7].va==0x492becu&&calls[7].arg[0]==VA(0x49878au)&&calls[7].arg[1]==0x7100u&&calls[7].edx==0x7100u);
    CHECK(calls[9].va==0x46a03cu&&calls[9].arg[0]==0xa9u&&calls[9].ecx==0xa8u);
    CHECK(calls[10].va==0x48349cu&&calls[10].arg[0]==0xa9u&&calls[10].arg[1]==3&&calls[10].eax==OBJ+0x80u&&calls[10].edx==0xdau&&calls[10].ecx==0xa9u);
    CHECK(r32(0,VA(0x4a9804u))==HEAP&&r32(0,VA(0x4a9808u))==0xaau&&r32(0,OBJ+0x80u)==42);
    /* EAX/ECX of the bitmap release, EDX = the record's saved head */
    CHECK(lo(p)==0xabu&&hi(p)==0x1111u&&ecx==0xcbu);
}

static void test_mech_forms(void){
    uint32_t ecx=0;
    setup();
    for(unsigned i=0;i<7;++i)w32(0,VA(0x49a360u)+4*i,0x8000u+i);
    static const uint8_t bytes[2+56]={2,0};stream_bytes=bytes;
    uint64_t p=es2_mech_forms_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==7+28+1);
    CHECK(calls[1].va==0x49681au&&calls[1].arg[0]==VA(0x4a9d08u)&&calls[1].arg[1]==0xffu&&calls[1].arg[2]==0x55u&&calls[1].eax==0x5555u);
    CHECK(arena[VA(0x4a9d08u)-BASE]==0xff&&arena[VA(0x4a9d08u)+0x54u-BASE]==0xff&&arena[VA(0x4a9d08u)+0x55u-BASE]==0xcc&&r16(0,VA(0x4a9d06u))==0);
    CHECK(calls[5].va==0x49667cu&&calls[5].count==5&&calls[5].arg[0]==0&&calls[5].arg[1]==0x1cu&&calls[5].arg[2]==2&&
        calls[5].arg[3]==1&&calls[5].arg[4]==VA(0x4207bbu)&&calls[5].eax==2);
    CHECK(calls[6].va==0x4735b8u&&calls[6].arg[1]==56&&calls[6].arg[2]==HEAP&&calls[6].eax==S&&calls[6].edx==2&&calls[6].ecx==56);
    for(unsigned i=0;i<7;++i){
        const call *k=calls+7+4*i;
        CHECK(k[0].va==0x492becu&&k[0].arg[0]==0x8000u+i&&k[0].arg[1]==0x7100u&&k[0].eax==0x7100u&&k[0].edx==0x8000u+i);
        CHECK(k[0].ecx==(i?0u:0xc7u));
        CHECK(k[1].va==0x45cdf4u&&k[2].va==0x46a03cu&&k[2].arg[0]==k[1].arg[0]+1u&&k[2].ecx==k[1].arg[0]+1u);
        CHECK(k[3].va==0x48349cu&&k[3].arg[0]==k[2].arg[0]&&k[3].arg[1]==3&&k[3].ecx==k[2].arg[0]);
        CHECK(r32(0,VA(0x4a9d72u)+8*i)==0&&r32(0,VA(0x4a9d76u)+8*i)==k[2].arg[0]+1u);
    }
    CHECK(r32(0,OBJ+0x80u)==48&&r32(0,VA(0x4a9d6cu))==HEAP);
    CHECK(calls[35].va==0x472f64u&&calls[35].eax==S&&calls[35].ecx==0);
    CHECK(lo(p)==0xa0u+36u&&hi(p)==0x1111u&&ecx==0xc0u+36u);
}

static void test_flyer_forms(void){
    uint32_t ecx=0;
    setup();
    static const uint8_t bytes[2+54]={3,0};stream_bytes=bytes;
    uint64_t p=es2_flyer_forms_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==8);
    CHECK(calls[1].va==0x49681au&&calls[1].arg[0]==VA(0x4a9dccu)&&calls[1].arg[2]==0x22u&&calls[1].edx==0xe1&&calls[1].ecx==0xe2);
    CHECK(calls[5].va==0x49667cu&&calls[5].arg[1]==0x12u&&calls[5].arg[2]==3&&calls[5].arg[4]==VA(0x423078u));
    CHECK(calls[6].va==0x4735b8u&&calls[6].arg[1]==54&&calls[6].eax==54&&calls[6].edx==54&&calls[6].ecx==S);
    CHECK(r32(0,VA(0x4a9dfcu))==HEAP&&r16(0,VA(0x4a9dcau))==0&&arena[VA(0x4a9dccu)+0x21u-BASE]==0xff&&arena[VA(0x4a9dccu)+0x22u-BASE]==0xcc);
    CHECK(lo(p)==0xa8u&&hi(p)==0x1111u&&ecx==0xc8u);
    /* the open failed: 479c78(6c56c8, 5, 0, "flyersys.cpp", 26h, name), EDX = 0 */
    setup();w32(0,F-0x2cu,9);stream_bytes=bytes;
    p=es2_flyer_forms_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==9&&calls[4].va==0x479c78u&&calls[4].count==6&&calls[4].arg[0]==VA(0x6c56c8u)&&calls[4].arg[1]==5&&
        calls[4].arg[2]==0&&calls[4].arg[3]==VA(0x49a693u)&&calls[4].arg[4]==0x26u&&calls[4].arg[5]==0xa3u&&calls[4].eax==9&&calls[4].edx==0);
    CHECK(calls[5].va==0x4735b8u&&calls[5].eax==0xa5u&&calls[5].edx==0xd5u);
}

static void test_weapon_types(void){
    uint32_t ecx=0;
    setup();
    for(unsigned i=0;i<4;++i)w32(0,VA(0x498eb0u)+4*i,0x9000u+i);
    /* weapons: 2 entries; proj: 1 record of 36 bytes */
    static uint8_t bytes[2+2+36];bytes[0]=2;bytes[2]=1;stream_bytes=bytes;shape_count=3;
    uint64_t p=es2_weapon_types_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==20);
    CHECK(calls[1].va==0x492becu&&calls[1].arg[0]==VA(0x4991f5u)&&calls[1].arg[1]==0x7100u&&calls[1].edx==0x7100u);
    CHECK(calls[2].va==0x45cdf4u&&calls[2].arg[0]==0xa2u&&calls[2].eax==0xa2u);
    CHECK(calls[3].va==0x46a03cu&&calls[3].arg[0]==0xa3u&&calls[3].ecx==0xa2u);
    CHECK(calls[4].va==0x48349cu&&calls[4].arg[0]==0xa3u&&calls[4].arg[1]==3&&calls[4].eax==OBJ+0x80u&&calls[4].edx==0xa3u&&calls[4].ecx==0xa3u);
    CHECK(r32(0,VA(0x4a9ae8u))==0&&r32(0,VA(0x4a9aecu))==0xa4u&&r32(0,OBJ+0x80u)==42);
    CHECK(calls[5].va==0x492becu&&calls[5].arg[0]==VA(0x4991fcu)&&calls[5].arg[1]==0x7200u&&calls[5].eax==VA(0x4991fcu)&&calls[5].ecx==0);
    CHECK(calls[6].va==0x474cd8u&&calls[6].arg[0]==VA(0x4a98f4u)&&calls[6].arg[1]==0xa6u&&calls[6].eax==0xa6u);
    CHECK(r16(0,VA(0x4a98f8u))==3&&r32(0,SHAPES+0x40u+0x26u)==VA(0x4a9ae8u)&&r32(0,SHAPES+0xc0u+0x26u)==VA(0x4a9ae8u)&&
        r32(0,SHAPES+0x100u+0x26u)==0xccccccccu);
    /* weapons: EDX = the last shape, ECX = sx(count) */
    CHECK(calls[7].va==0x492becu&&calls[7].arg[0]==VA(0x499205u)&&calls[7].eax==0x7300u&&calls[7].edx==SHAPES+0xc0u&&calls[7].ecx==3);
    check_open(8,0xa8u);
    CHECK(calls[9].va==0x4735b8u&&calls[9].arg[2]==VA(0x4a98f0u)&&calls[9].ecx==S);
    CHECK(calls[10].va==0x49667cu&&calls[10].arg[1]==0x58u&&calls[10].arg[2]==2&&calls[10].arg[4]==VA(0x41178du));
    CHECK(calls[11].va==0x40f8bcu&&calls[11].arg[0]==HEAP&&calls[11].arg[1]==S&&calls[11].eax==S&&calls[11].edx==0xdbu&&calls[11].ecx==2);
    CHECK(calls[12].va==0x40f8bcu&&calls[12].arg[0]==HEAP+0x58u&&calls[12].edx==0x9000u&&calls[12].ecx==2);
    CHECK(r32(0,HEAP+0x52u)==0x9000u&&r16(0,HEAP+0x56u)==0&&r32(0,HEAP+0x58u+0x52u)==0x9001u&&r16(0,HEAP+0x58u+0x56u)==1);
    CHECK(calls[13].va==0x47307cu&&calls[13].arg[0]==S&&calls[13].eax==S&&calls[13].edx==0x9001u&&calls[13].ecx==2);
    CHECK(calls[14].va==0x492becu&&calls[14].arg[0]==VA(0x499219u)&&calls[14].ecx==S&&calls[14].edx==0x7300u);
    CHECK(calls[15].va==0x4730e8u&&calls[15].arg[0]==S&&calls[15].arg[1]==0xafu&&calls[15].eax==S);
    CHECK(calls[16].va==READ_METHOD&&calls[16].arg[2]==VA(0x4a9900u)&&calls[16].eax==0xb0u&&calls[16].edx==S&&calls[16].ecx==TABLE);
    CHECK(calls[17].va==0x4774f0u&&calls[17].arg[0]==36&&calls[17].eax==36);
    CHECK(calls[18].va==0x4735b8u&&calls[18].arg[1]==36&&calls[18].eax==36&&calls[18].edx==S&&calls[18].ecx==36);
    CHECK(calls[19].va==0x472f64u&&calls[19].eax==0x101u&&calls[19].ecx==S);
    CHECK(r32(0,VA(0x4a98ecu))==HEAP&&r32(0,VA(0x4a98fcu))==HEAP+0xb0u+4u);
    CHECK(lo(p)==0x1111u&&hi(p)==0xd0u+20u&&ecx==0xc0u+20u);
}

int main(void){
    test_weapon_entry();
    test_rocket_types();
    test_bullet_types();
    test_beam_types();
    test_mech_forms();
    test_flyer_forms();
    test_weapon_types();
    printf("PASS: stage2_types %u checks\n",checks);
    return 0;
}
