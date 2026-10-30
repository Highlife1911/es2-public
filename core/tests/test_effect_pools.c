#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/effect_pools.h"
/* Synthetic state for the core (the effect pools with resources).
 * The image base is placed so that the globals 497000..6cc000 fall inside
 * the arena. A logging call function stands in for every game call
 * (target, arguments, count, entry EAX/EDX/ECX, the context word): new
 * 47749c, new[] 4774f0 and the array construction 49667c return the next
 * block of a bump area (new can be made to fail), the stream constructor
 * 472f14 writes the stream's table and a scripted status, stream_read
 * 4735b8 and the read method copy from a scripted byte stream, the shape
 * loader 474cd8 stores a list of shapes and returns a scripted count, and
 * every other call returns EAX = A0h+n, EDX = D0h+n, ECX = C0h+n (n = call
 * number, from 1). Expected values are worked out by hand from the contract
 * in mission_load.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define HEAP (BASE+0x300000u)
#define OBJ (BASE+0x2f0000u)
#define SHAPES (OBJ+0x3000u)
#define READ_METHOD UINT32_C(0x77700000)
static uint8_t arena[0x310000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"effect_pools line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct call { uint32_t va,arg[6],count,eax,edx,ecx,context; } call;
static call calls[128];static unsigned call_count,observed,slot_seen,new_fails;
static uint32_t heap_next,status_stream,context_at,shape_count;
static const uint8_t *stream_bytes;static unsigned stream_at;
static uint32_t va_of(uint32_t f){return f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;}
#define SCRATCH (OBJ+0x1000u)
#define TABLE (OBJ+0x200u)
static uint32_t bump(uint32_t size){const uint32_t p=heap_next;heap_next+=(size+7u)&~3u;return p;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<128);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=va_of(f);for(unsigned i=0;i<n&&i<6;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    k->context=context_at?r16(0,context_at):0;
    r->eax=0xa0u+call_count;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;
    if(k->va==0x4774f0u)r->eax=bump(a[0]);
    else if(k->va==0x47749cu)r->eax=new_fails?(--new_fails,0u):bump(a[0]);
    else if(k->va==0x49667cu)r->eax=bump(a[1]*a[2]);
    else if(k->va==0x472f14u){w32(0,a[0],TABLE);w32(0,a[0]+4u,a[0]==status_stream?7u:0u);r->eax=a[0];}
    else if(f==READ_METHOD||k->va==0x4735b8u){
        CHECK(a[1]<0x1000u);
        for(uint32_t i=0;i<a[1];++i)((uint8_t *)host(0,a[2]+i))[0]=stream_bytes[stream_at++];
        r->eax=0x101u;
    } else if(k->va==0x474cd8u){
        w32(0,a[0],SHAPES);
        for(uint32_t i=0;i<8;++i)w32(0,SHAPES+4u*i,SHAPES+0x40u+0x40u*i);
        r->eax=0x70000u|shape_count;
    } else if(k->va==0x49630cu){r->eax=0x5555u;r->edx=k->edx;r->ecx=k->ecx;}   /* EDX/ECX kept */
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;if(slot)++slot_seen;++observed;}
static es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(uint32_t frame,uint32_t context){
    memset(arena,0xcc,sizeof(arena));call_count=observed=slot_seen=new_fails=0;heap_next=HEAP;stream_at=0;status_stream=0;
    graph.scratch=SCRATCH;context_at=context?SCRATCH+frame-context:0;shape_count=3;
    w32(0,SCRATCH+frame-(frame==0x6cu||frame==0x74u?0x58u:frame),0x1111u);w32(0,TABLE+0x18u,READ_METHOD);
    w32(0,VA(0x4a0a10u),0x7100u);w32(0,VA(0x4a09f0u),0x7300u);w32(0,VA(0x4a09f8u),0x7500u);
    w32(0,VA(0x6cbee4u),OBJ+0x80u);w32(0,OBJ+0x80u,41);w16(0,VA(0x4d2680u),0);
}
#define RA(i) (0xa1u+(i))
#define RD(i) (0xd1u+(i))
#define RC(i) (0xc1u+(i))
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}
static int is_call(unsigned i,uint32_t va,unsigned count){return calls[i].va==va&&calls[i].count==count;}
static int is_read(unsigned i,uint32_t S,uint32_t size,uint32_t buf){
    return calls[i].va==READ_METHOD&&calls[i].count==3&&calls[i].arg[0]==S&&calls[i].arg[1]==size&&calls[i].arg[2]==buf;
}
static int is_sread(unsigned i,uint32_t S,uint32_t size,uint32_t buf){
    return calls[i].va==0x4735b8u&&calls[i].count==3&&calls[i].arg[0]==S&&calls[i].arg[1]==size&&calls[i].arg[2]==buf;
}
static uint8_t script[512];static unsigned script_size;
static void put(const void *p,unsigned n){memcpy(script+script_size,p,n);script_size+=n;}
static void put16(uint16_t v){put(&v,2);}
static void piece(uint16_t w0,uint16_t w2,uint16_t w8,uint16_t wa){put16(w0);put16(w2);put16(0x33);put16(0x44);put16(w8);put16(wa);put16(0x55);}
/* The pool and list steps (calls i..i+3 when both new succeed). */
static void check_pools(unsigned i,uint32_t capacity,uint32_t size,uint32_t list,uint32_t pool_global,uint32_t list_global){
    CHECK(is_call(i,0x47749cu,1)&&calls[i].arg[0]==0x12u);
    CHECK(is_call(i+1,0x471ad8u,3)&&calls[i+1].arg[0]==HEAP&&calls[i+1].arg[1]==capacity&&
        calls[i+1].arg[2]==size&&calls[i+1].context==0x14);
    CHECK(is_call(i+2,0x47749cu,1)&&calls[i+2].arg[0]==8&&calls[i+2].context==8);
    CHECK(is_call(i+3,0x4774f0u,1)&&calls[i+3].arg[0]==4u*list&&calls[i+3].context==0x20&&calls[i+3].ecx==8);
    CHECK(r32(0,VA(pool_global))==HEAP&&r32(0,VA(list_global))==HEAP+0x18u);
    CHECK(r32(0,HEAP+0x18u)==HEAP+0x24u&&r16(0,HEAP+0x18u+4u)==0&&r16(0,HEAP+0x18u+6u)==list);
}

/* debris_record: two pieces; +8 and +0Ah x 182 unless FFFFh; the sum of
 * the words +2; AX = the largest word +0; ECX = maximum:counter. */
static void test_debris_record(void){
    uint32_t ecx=0;setup(0,0);
    script_size=0;put16(5);put16(2);piece(3,10,2,0xffffu);piece(9,20,0xffffu,1);stream_bytes=script;
    const uint32_t R=OBJ+0x400u,S=OBJ+0x500u;w32(0,S,TABLE);
    const uint64_t p=es2_debris_record_registers(&memory,&graph,R,S,entry,&ecx);
    CHECK(call_count==17&&observed==17&&slot_seen==16&&stream_at==script_size);
    CHECK(is_read(0,S,2,R)&&calls[0].eax==R&&calls[0].edx==TABLE&&calls[0].ecx==0xe2);
    CHECK(is_read(1,S,2,R+2u)&&calls[1].eax==R+2u&&calls[1].edx==TABLE&&calls[1].ecx==RC(0));
    CHECK(is_call(2,0x4774f0u,1)&&calls[2].arg[0]==28&&calls[2].eax==28&&calls[2].ecx==2);
    const uint32_t P=HEAP;
    CHECK(r32(0,R+4u)==P&&r16(0,R)==5&&r16(0,R+2u)==2);
    for(unsigned k=0;k<7;++k)CHECK(is_read(3+k,S,2,P+2u*k)&&calls[3+k].eax==P+2u*k&&calls[3+k].edx==TABLE);
    CHECK(calls[3].ecx==28);
    /* piece 1: EDX = the count word 2 over the read's EDX, ECX = 364 */
    CHECK(is_read(10,S,2,P+14u)&&calls[10].ecx==364u);
    for(unsigned k=0;k<7;++k)CHECK(is_read(10+k,S,2,P+14u+2u*k));
    CHECK(r16(0,P+8u)==364&&r16(0,P+0xau)==0xffffu&&r16(0,P+14u+8u)==0xffffu&&r16(0,P+14u+0xau)==182);
    CHECK(r16(0,R+8u)==30);
    CHECK(lo(p)==9&&hi(p)==2&&ecx==(9u<<16|2u));
    /* no pieces: AX = 0 over new[]'s high word, ECX = 0:0 */
    setup(0,0);script_size=0;put16(1);put16(0);stream_bytes=script;w32(0,S,TABLE);
    const uint64_t q=es2_debris_record_registers(&memory,&graph,R,S,entry,&ecx);
    CHECK(call_count==3&&calls[2].arg[0]==0&&lo(q)==(HEAP&0xffff0000u)&&hi(q)==(RD(2)&0xffff0000u)&&ecx==0);
    /* negative word +0: the maximum stays 0 */
    setup(0,0);script_size=0;put16(1);put16(1);piece(0xfff0u,4,0,0);stream_bytes=script;w32(0,S,TABLE);
    const uint64_t s=es2_debris_record_registers(&memory,&graph,R,S,entry,&ecx);
    CHECK(call_count==10&&lo(s)==((RA(9)&0xffff0000u)|0)&&ecx==1u&&r16(0,R+8u)==4&&r16(0,HEAP+8u)==0);
}

/* debris_set: the shapes, one record, +26h of three shapes = the tag. */
static void test_debris_set(void){
    uint32_t ecx=0;setup(ES2_DEBRIS_SET_FRAME,0x40u);
    const uint32_t F=SCRATCH+ES2_DEBRIS_SET_FRAME,S=F-0x30u,set=OBJ+0x600u;
    w32(0,F-0x50u,0x3333u);
    script_size=0;put16(1);stream_bytes=script;
    const uint64_t p=es2_debris_set_registers(&memory,&graph,set,0x4444u,0x9999u,entry,&ecx);
    CHECK(call_count==9&&observed==9&&slot_seen==1);
    CHECK(is_call(0,0x49630cu,0)&&calls[0].eax==VA(0x497da0u)&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(is_call(1,0x492becu,2)&&calls[1].arg[0]==0x4444u&&calls[1].arg[1]==VA(0x497eb4u));
    CHECK(is_call(2,0x474cd8u,4)&&calls[2].arg[0]==set+6u&&calls[2].arg[1]==RA(1)&&calls[2].arg[2]==0x140008u&&
        calls[2].arg[3]==0&&calls[2].eax==set+6u);
    CHECK(r16(0,set+0xau)==3&&r32(0,set+6u)==SHAPES);
    CHECK(is_call(3,0x492becu,2)&&calls[3].arg[0]==0x4444u&&calls[3].arg[1]==0x7300u&&calls[3].edx==0x7300u);
    CHECK(is_call(4,0x472f14u,2)&&calls[4].arg[0]==S&&calls[4].arg[1]==RA(3)&&calls[4].eax==S&&calls[4].context==8);
    CHECK(is_read(5,S,2,set+4u)&&calls[5].eax==set+4u&&calls[5].edx==S&&calls[5].ecx==TABLE);
    CHECK(is_call(6,0x4774f0u,1)&&calls[6].arg[0]==10&&calls[6].eax==1&&calls[6].edx==10);
    CHECK(r32(0,set)==HEAP);
    CHECK(is_call(7,0x4083f8u,2)&&calls[7].arg[0]==HEAP&&calls[7].arg[1]==S&&calls[7].eax==S&&calls[7].edx==1&&calls[7].ecx==10);
    for(unsigned k=0;k<3;++k)CHECK(r32(0,SHAPES+0x40u+0x40u*k+0x26u)==0x9999u);
    CHECK(r32(0,SHAPES+0x40u+0x40u*3+0x26u)==0xccccccccu);
    /* stream_destroy: EAX = the count word 3 of the last check, EDX = S, ECX = the third shape */
    CHECK(is_call(8,0x472f64u,2)&&calls[8].arg[0]==S&&calls[8].arg[1]==2&&calls[8].eax==3&&calls[8].edx==S&&
        calls[8].ecx==SHAPES+0xc0u);
    CHECK(lo(p)==RA(8)&&hi(p)==RD(8)&&ecx==0x3333u);
    /* no tag, a failed open, no record */
    setup(ES2_DEBRIS_SET_FRAME,0x40u);w32(0,F-0x50u,0x3333u);status_stream=S;
    script_size=0;put16(0);stream_bytes=script;
    (void)es2_debris_set_registers(&memory,&graph,set,0x4444u,0,entry,&ecx);
    CHECK(call_count==9&&is_call(5,0x479c78u,6)&&calls[5].arg[0]==VA(0x6c56c8u)&&calls[5].arg[1]==5&&calls[5].arg[2]==0&&
        calls[5].arg[3]==VA(0x497eb8u)&&calls[5].arg[4]==0xa1u&&calls[5].arg[5]==RA(3)&&calls[5].eax==7&&calls[5].edx==0);
    CHECK(is_call(8,0x472f64u,2)&&calls[8].eax==HEAP&&calls[8].edx==S&&calls[8].ecx==0);
    CHECK(r32(0,SHAPES+0x40u+0x26u)==0xccccccccu&&ecx==0x3333u);
}

/* debris_pools: the pool, the list, the default debris set, the words. */
static void test_debris_pools(void){
    uint32_t ecx=0;setup(ES2_DEBRIS_POOLS_FRAME,0x18u);
    const uint32_t F=SCRATCH+ES2_DEBRIS_POOLS_FRAME;w32(0,F-0x28u,0x2222u);
    const uint64_t p=es2_debris_pools_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==6&&is_call(0,0x49630cu,0)&&calls[0].eax==VA(0x497dd4u));
    check_pools(1,0x96u,0x61u,0x28u,0x4a9642u,0x4a9646u);
    CHECK(calls[2].edx==HEAP&&calls[2].ecx==0x12u&&calls[4].edx==(0x28u<<2));
    CHECK(is_call(5,0x40874cu,3)&&calls[5].arg[0]==VA(0x4a9650u)&&calls[5].arg[1]==VA(0x497ec3u)&&calls[5].arg[2]==0&&
        calls[5].eax==HEAP+0x18u&&calls[5].edx==HEAP+0x18u&&calls[5].ecx==HEAP+0x18u);
    CHECK(r32(0,VA(0x4a964cu))==0&&r16(0,VA(0x4a9664u))==6000&&r16(0,VA(0x4a9666u))==16000&&r16(0,VA(0x4a9668u))==420);
    CHECK(lo(p)==0&&hi(p)==RD(5)&&ecx==0x2222u);
    /* both new fail: the globals 0, EDX = 0 */
    setup(ES2_DEBRIS_POOLS_FRAME,0x18u);w32(0,F-0x28u,0x2222u);new_fails=2;
    (void)es2_debris_pools_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==4&&r32(0,VA(0x4a9642u))==0&&r32(0,VA(0x4a9646u))==0&&calls[3].eax==0&&calls[3].edx==0&&calls[3].ecx==8);
}

/* explosion_pools: 15 bitmaps (low-memory word 0), the shapes and their map,
 * two records; 4a9634h = max(word +0) + 1; +26h from the map words. */
static void test_explosion_pools(void){
    uint32_t ecx=0;setup(ES2_EXPLOSION_POOLS_FRAME,0x48u);
    const uint32_t F=SCRATCH+ES2_EXPLOSION_POOLS_FRAME,S=F-0x38u;
    memcpy(host(0,VA(0x497ba0u)),"explo666\0\0\0\0\0\0\0",16);
    script_size=0;put16(3);put16(3);put16(0);put16(2);put16(0);put16(1);put16(0);   /* the map count, the map (3 x 4 bytes) */
    put16(2);                                                                               /* two records */
    {uint8_t rec[80];memset(rec,0,80);rec[0]=6;rec[40]=0xfe;rec[41]=0xff;put(rec,80);}
    stream_bytes=script;
    const uint64_t p=es2_explosion_pools_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==62&&stream_at==script_size&&slot_seen==1);
    CHECK(memcmp(host(0,F-0x6cu),"explo666",8)==0);
    CHECK(is_call(1,0x47749cu,1)&&calls[1].ecx==0);   /* REP MOVSD left ECX = 0 */
    check_pools(1,0x28u,0x5bu,0x1eu,0x4a961eu,0x4a9622u);
    CHECK(is_call(5,0x49667cu,5)&&calls[5].arg[1]==8&&calls[5].arg[2]==15&&calls[5].arg[4]==VA(0x46bbc1u));
    const uint32_t pairs=HEAP+0xa0u;
    CHECK(r32(0,VA(0x4a9638u))==pairs);
    for(unsigned k=0;k<15;++k){
        const unsigned i=6+3*k;
        CHECK(is_call(i,0x496820u,3)&&calls[i].arg[0]==k&&calls[i].arg[1]==F-0x67u&&calls[i].arg[2]==10&&calls[i].eax==F-0x67u);
        CHECK(is_call(i+1,0x492becu,2)&&calls[i+1].arg[0]==F-0x6cu&&calls[i+1].arg[1]==0x7100u&&calls[i+1].edx==0x7100u);
        CHECK(is_call(i+2,0x45cdf4u,1)&&calls[i+2].arg[0]==RA(i+1));
        CHECK(r32(0,pairs+8u*k)==RA(i+2));
    }
    CHECK(is_call(51,0x492becu,2)&&calls[51].arg[0]==VA(0x497d2eu)&&calls[51].arg[1]==0x7500u&&calls[51].ecx==pairs);
    CHECK(is_call(52,0x474cd8u,4)&&calls[52].arg[0]==VA(0x4a9630u)&&calls[52].arg[1]==RA(51));
    CHECK(is_call(53,0x4774f0u,1)&&calls[53].arg[0]==12);
    const uint32_t map=HEAP+0x11cu;
    CHECK(r32(0,VA(0x4a963cu))==map);
    CHECK(is_call(54,0x492becu,2)&&calls[54].arg[0]==VA(0x497d2eu)&&calls[54].arg[1]==0x7300u&&calls[54].ecx==12);
    CHECK(is_call(55,0x472f14u,2)&&calls[55].arg[0]==S&&calls[55].arg[1]==RA(54)&&calls[55].context==0x2c);
    CHECK(is_read(56,S,2,F-0x5au)&&calls[56].eax==S&&calls[56].edx==TABLE&&calls[56].ecx==F-0x5au);
    CHECK(is_sread(57,S,12,map)&&calls[57].eax==12&&calls[57].edx==S&&calls[57].ecx==map);
    CHECK(is_sread(58,S,2,VA(0x4a962cu))&&calls[58].ecx==S);
    CHECK(is_call(59,0x4774f0u,1)&&calls[59].arg[0]==80&&calls[59].eax==2);
    const uint32_t records=HEAP+0x12cu;
    CHECK(r32(0,VA(0x4a9628u))==records);
    CHECK(is_sread(60,S,80,records)&&calls[60].edx==80&&calls[60].ecx==S);
    CHECK(is_call(61,0x472f64u,2)&&calls[61].arg[0]==S&&calls[61].arg[1]==2&&calls[61].eax==S);
    /* records: word +0 = 6 and -2: 7, then max(-1, 7) */
    CHECK(r16(0,VA(0x4a9634u))==7);
    /* the map words +2: 0, 0, 0 for shapes 0..2 (the words 3, 2, 1 are +0) */
    for(unsigned k=0;k<3;++k)CHECK(r32(0,SHAPES+0x40u+0x40u*k+0x26u)==pairs);
    CHECK(r32(0,SHAPES+0x40u+0x40u*3+0x26u)==0xccccccccu);
    CHECK(lo(p)==0x1111u&&hi(p)==3&&ecx==SHAPES+0xc0u);
    /* low-memory word set (-l): the suffix, 13 bitmaps, the second shape name */
    setup(ES2_EXPLOSION_POOLS_FRAME,0x48u);w16(0,VA(0x4d2680u),1);stream_at=0;
    (void)es2_explosion_pools_registers(&memory,&graph,entry,&ecx);
    CHECK(is_call(6,0x4967eau,2)&&calls[6].arg[0]==F-0x6cu&&calls[6].arg[1]==VA(0x497d24u));
    CHECK(is_call(7+3*12,0x496820u,3)&&calls[7+3*12].arg[0]==12&&is_call(7+39,0x492becu,2)&&calls[46].arg[0]==VA(0x497d26u));
    /* a failed open reports with EDX = 0 */
    setup(ES2_EXPLOSION_POOLS_FRAME,0x48u);status_stream=S;stream_at=0;
    (void)es2_explosion_pools_registers(&memory,&graph,entry,&ecx);
    CHECK(is_call(56,0x479c78u,6)&&calls[56].arg[2]==0&&calls[56].arg[3]==VA(0x497d35u)&&calls[56].arg[4]==0x56u&&
        calls[56].arg[5]==RA(54)&&calls[56].eax==7&&calls[56].edx==0);
}

/* fire_pools: the copied base name, the shapes, the per-shape bytes, two
 * bitmap pairs. */
static void test_fire_pools(void){
    uint32_t ecx=0;setup(ES2_FIRE_POOLS_FRAME,0x48u);
    const uint32_t F=SCRATCH+ES2_FIRE_POOLS_FRAME,S=F-0x38u;
    memcpy(host(0,VA(0x4a0a74u)),"fire",5);
    script_size=0;{static const uint8_t c[4]={1,2,3,4};put(c,4);}{static const uint8_t b[3]={1,0,0xff};put(b,3);}
    stream_bytes=script;
    const uint64_t p=es2_fire_pools_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==22&&stream_at==7&&slot_seen==2);
    check_pools(1,0xau,0x5bu,0xau,0x6b4f30u,0x6b4f34u);
    CHECK(memcmp(host(0,F-0x60u),"fire",5)==0);
    /* the copy left EAX = the name buffer, EDX = 5, ECX = 0 */
    CHECK(is_call(5,0x492becu,2)&&calls[5].arg[0]==F-0x74u&&calls[5].arg[1]==0x7500u&&calls[5].eax==F-0x74u&&
        calls[5].edx==0x7500u&&calls[5].ecx==F-0x74u);
    CHECK(is_call(6,0x474cd8u,4)&&calls[6].arg[0]==VA(0x6b4f28u));
    CHECK(is_call(7,0x492becu,2)&&calls[7].arg[0]==F-0x60u&&calls[7].arg[1]==0x7300u&&calls[7].context==0x2c);
    CHECK(is_call(8,0x472f14u,2)&&calls[8].arg[0]==S&&calls[8].ecx==S);
    CHECK(is_read(9,S,4,F-0x64u)&&calls[9].eax==F-0x64u&&calls[9].edx==S&&calls[9].ecx==TABLE);
    CHECK(is_call(10,0x4774f0u,1)&&calls[10].arg[0]==3);
    const uint32_t bytes=HEAP+0x50u;
    CHECK(is_read(11,S,3,bytes)&&calls[11].eax==TABLE&&calls[11].edx==3&&calls[11].ecx==S);
    CHECK(is_call(12,0x472f64u,2)&&calls[12].edx==S);
    CHECK(is_call(13,0x49667cu,5)&&calls[13].arg[2]==2);
    const uint32_t pairs=HEAP+0x58u;
    for(unsigned k=0;k<2;++k){
        const unsigned i=14+4*k;
        CHECK(is_call(i,0x496820u,3)&&calls[i].arg[0]==k&&calls[i].arg[1]==F-0x66u&&calls[i].eax==F-0x66u&&
            calls[i].edx==5&&calls[i].ecx==0);
        CHECK(is_call(i+1,0x4967eau,2)&&calls[i+1].arg[0]==F-0x74u&&calls[i+1].arg[1]==RA(i)&&calls[i+1].edx==F-0x74u);
        CHECK(is_call(i+2,0x492becu,2)&&calls[i+2].arg[0]==F-0x74u&&calls[i+2].arg[1]==0x7100u&&calls[i+2].edx==F-0x74u);
        CHECK(is_call(i+3,0x45cdf4u,1)&&r32(0,pairs+8u*k)==RA(i+3));
    }
    CHECK(memcmp(host(0,F-0x74u),"fire",5)==0);
    CHECK(r32(0,SHAPES+0x40u+0x26u)==pairs+8u&&r32(0,SHAPES+0x80u+0x26u)==pairs&&r32(0,SHAPES+0xc0u+0x26u)==pairs-8u);
    CHECK(r32(0,VA(0x6b4f38u))==0&&lo(p)==0x1111u&&hi(p)==3&&ecx==SHAPES+0xc0u);
}

/* flat_pools: the guard byte, flat2 for a nonzero flag, the bitmap pair,
 * +26h = the pair, the pool and list (ECX/EDX variants). */
static void test_flat_pools(void){
    uint32_t ecx=0;setup(ES2_EFFECT_POOL_FRAME,0x18u);
    const uint32_t F=SCRATCH+ES2_EFFECT_POOL_FRAME;w32(0,F-0x28u,0x2222u);
    ((uint8_t *)host(0,VA(0x498098u)))[0]=0;
    const uint64_t p=es2_flat_pools_registers(&memory,&graph,0x50000u,entry,&ecx);
    CHECK(call_count==11&&((uint8_t *)host(0,VA(0x498098u)))[0]==1);
    CHECK(is_call(1,0x492becu,2)&&calls[1].arg[0]==VA(0x49810fu)&&calls[1].arg[1]==0x7500u);
    CHECK(is_call(2,0x474cd8u,4)&&calls[2].arg[0]==VA(0x4a9698u)&&r16(0,VA(0x4a969cu))==3);
    CHECK(is_call(3,0x492becu,2)&&calls[3].arg[0]==VA(0x49810fu)&&calls[3].arg[1]==0x7100u);
    CHECK(is_call(5,0x46a03cu,1)&&calls[5].arg[0]==RA(4));
    CHECK(is_call(6,0x48349cu,2)&&calls[6].arg[0]==RA(4)&&calls[6].arg[1]==3&&calls[6].eax==OBJ+0x80u&&calls[6].edx==RA(4));
    CHECK(r32(0,OBJ+0x80u)==42&&r32(0,VA(0x4a969eu))==0&&r32(0,VA(0x4a96a2u))==RA(5));
    for(unsigned k=0;k<3;++k)CHECK(r32(0,SHAPES+0x40u+0x40u*k+0x26u)==VA(0x4a969eu));
    CHECK(is_call(7,0x47749cu,1)&&calls[7].eax==3&&calls[7].ecx==SHAPES+0x0u);
    CHECK(is_call(8,0x471ad8u,3)&&calls[8].arg[0]==HEAP&&calls[8].arg[1]==0x96u&&calls[8].arg[2]==0x41u&&calls[8].ecx==HEAP&&
        calls[8].edx==RD(7));
    CHECK(is_call(10,0x4774f0u,1)&&calls[10].arg[0]==100&&calls[10].eax==100&&calls[10].ecx==(RC(9)&0xffff0000u|0x19u));
    CHECK(r32(0,VA(0x4a968du))==HEAP&&r32(0,VA(0x4a9691u))==HEAP+0x18u&&r16(0,HEAP+0x18u+6u)==25);
    CHECK(lo(p)==0x2222u&&hi(p)==HEAP+0x18u&&ecx==100);
    /* flag word nonzero: flat; the guard byte already set stays */
    setup(ES2_EFFECT_POOL_FRAME,0x18u);((uint8_t *)host(0,VA(0x498098u)))[0]=7;
    (void)es2_flat_pools_registers(&memory,&graph,1,entry,&ecx);
    CHECK(calls[1].arg[0]==VA(0x498109u)&&((uint8_t *)host(0,VA(0x498098u)))[0]==7);
}

/* meteor_pools and smoke_balls. */
static void test_shape_pools(void){
    uint32_t ecx=0;setup(ES2_EFFECT_POOL_FRAME,0x18u);
    const uint32_t F=SCRATCH+ES2_EFFECT_POOL_FRAME;w32(0,F-0x28u,0x2222u);
    uint64_t p=es2_meteor_pools_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==9&&is_call(0,0x49630cu,0)&&calls[0].eax==VA(0x498134u));
    check_pools(1,0xau,0x59u,0x19u,0x4a96aau,0x4a96aeu);
    CHECK(is_call(5,0x492becu,2)&&calls[5].arg[0]==VA(0x49829cu)&&calls[5].arg[1]==0x7500u&&calls[5].eax==0x7500u);
    CHECK(is_call(6,0x474cd8u,4)&&calls[6].arg[0]==VA(0x4a96b4u)&&calls[6].arg[1]==RA(5));
    CHECK(is_call(7,0x492becu,2)&&calls[7].arg[0]==VA(0x4982a3u)&&calls[7].arg[1]==0x7100u);
    CHECK(r32(0,VA(0x4a96b8u))==RA(8));
    for(unsigned k=0;k<3;++k)CHECK(r32(0,SHAPES+0x40u+0x40u*k+0x26u)==VA(0x4a96b8u));
    CHECK(lo(p)==0x2222u&&hi(p)==SHAPES+0xc0u&&ecx==SHAPES);
    /* no shapes: the loop does not run */
    setup(ES2_EFFECT_POOL_FRAME,0x18u);w32(0,F-0x28u,0x2222u);shape_count=0;
    p=es2_smoke_balls_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==9&&calls[0].eax==VA(0x497f12u));
    check_pools(1,0xc8u,0x4cu,0x19u,0x4a966eu,0x4a9672u);
    CHECK(calls[5].arg[0]==VA(0x49804au)&&calls[6].arg[0]==VA(0x4a9678u)&&calls[7].arg[0]==VA(0x498050u));
    CHECK(r32(0,VA(0x4a967cu))==RA(8)&&r32(0,SHAPES+0x40u+0x26u)==0xccccccccu);
    CHECK(lo(p)==0x2222u&&hi(p)==RD(8)&&calls[8].arg[0]==RA(7)&&ecx==RA(7));
}

int main(void){
    test_debris_record();
    test_debris_set();
    test_debris_pools();
    test_explosion_pools();
    test_fire_pools();
    test_flat_pools();
    test_shape_pools();
    printf("effect_pools: %u checks\n",checks);
    return 0;
}
