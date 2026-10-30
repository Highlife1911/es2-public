#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/building_types.h"
/* Synthetic state for the core (the building module's stage-2
 * callback base_types and the building pools base_pools). The image base is
 * placed so that the globals 497000..6cbee4 fall inside the arena. A
 * logging call function stands in for every game call (target, arguments,
 * count, entry EAX/EDX/ECX): new 47749c, new[] 4774f0 and the array
 * construction 49667c return the next block of a bump area (new can be
 * made to fail), the stream constructor 472f14 writes the stream's table
 * and a scripted status, stream_read 4735b8 and the read method copy from a
 * scripted byte stream, the shape loader 474cd8 stores a list of shapes, and
 * every other call returns EAX = A0h+n, EDX = D0h+n, ECX = C0h+n (n = call
 * number). Expected values are worked out by hand from the contract in
 * mission_load.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define HEAP (BASE+0x300000u)
#define OBJ (BASE+0x2f0000u)
#define SHAPES (OBJ+0x3000u)
#define READ_METHOD UINT32_C(0x77700000)
static uint8_t arena[0x310000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"building_types line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct call { uint32_t va,arg[6],count,eax,edx,ecx,context; } call;
static call calls[128];static unsigned call_count,observed,slot_seen,new_fails;
static uint32_t heap_next,status_stream,context_at;
static const uint8_t *stream_bytes;static unsigned stream_at;
static uint32_t va_of(uint32_t f){return f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;}
#define SCRATCH (OBJ+0x1000u)
#define F (SCRATCH+ES2_BASE_TYPES_FRAME)
#define PF (SCRATCH+ES2_BASE_POOLS_FRAME)
#define S1 (F-0x60u)
#define S2 (F-0x90u)
#define S3 (F-0x30u)
#define S4 (F-0xc0u)
#define TABLE (OBJ+0x200u)
static uint32_t bump(uint32_t size){const uint32_t p=heap_next;heap_next+=(size+7u)&~3u;return p;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<128);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=va_of(f);for(unsigned i=0;i<n&&i<6;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    k->context=r16(0,context_at);
    const uint32_t eax=0xa0u+call_count,edx=0xd0u+call_count,ecx=0xc0u+call_count;
    r->eax=eax;r->edx=edx;r->ecx=ecx;
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
        r->eax=0x70008u;
    } else if(k->va==0x49630cu){r->eax=0x5555u;r->edx=k->edx;r->ecx=k->ecx;}   /* EDX/ECX kept */
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;if(slot)++slot_seen;++observed;}
static es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(void){
    memset(arena,0xcc,sizeof(arena));call_count=observed=slot_seen=new_fails=0;heap_next=HEAP;stream_at=0;status_stream=0;
    graph.scratch=SCRATCH;context_at=F-0xd0u;
    w32(0,F-0xe0u,0x1111u);w32(0,PF-0x28u,0x2222u);w32(0,TABLE+0x18u,READ_METHOD);
    w32(0,VA(0x4a0a10u),0x7100u);w32(0,VA(0x4a09f0u),0x7300u);
    w32(0,VA(0x6cbee4u),OBJ+0x80u);w32(0,OBJ+0x80u,41);
}
/* The fake's results of call i: EAX, EDX, ECX. */
#define RA(i) (0xa1u+(i))
#define RD(i) (0xd1u+(i))
#define RC(i) (0xc1u+(i))
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}
static int is_read(unsigned i,uint32_t S,uint32_t size,uint32_t buf){
    return calls[i].va==READ_METHOD&&calls[i].count==3&&calls[i].arg[0]==S&&calls[i].arg[1]==size&&calls[i].arg[2]==buf;
}
static int is_sread(unsigned i,uint32_t S,uint32_t size,uint32_t buf){
    return calls[i].va==0x4735b8u&&calls[i].count==3&&calls[i].arg[0]==S&&calls[i].arg[1]==size&&calls[i].arg[2]==buf;
}

/* The scripted files: bforms with one form (2 elements, one byte list of
 * length 3), lc_wpns with 2 records, bases with 2 types (type 0: words +2
 * = 4, +4 = 1, +6 = 1, one child, +30h = 1; type 1: +2 = 2, +4 = 5, +6 =
 * 0, no child, +30h = 0). */
static uint8_t script[512];static unsigned script_size;
static void put(const void *p,unsigned n){memcpy(script+script_size,p,n);script_size+=n;}
static void put16(uint16_t v){put(&v,2);}
static void put32(uint32_t v){put(&v,4);}
static void zeros(unsigned n){static const uint8_t z[64];put(z,n);}
static void type_record(uint16_t w2,uint16_t w4,uint16_t w6,uint16_t children,uint16_t w30){
    put16(0x11);put16(w2);put16(w4);put16(w6);put16(0x22);   /* +0..+8 */
    zeros(6);put16(0x33);put16(children);                      /* +0Ah, +10h, +12h */
    for(unsigned i=0;i<children;++i){zeros(29);put("\x5a",1);} /* 1Eh bytes each */
    zeros(6);put16(0x44);zeros(4);put16(0x55);put16(0x66);    /* +18h, +1Eh, +20h, +24h, +26h */
    put16(1);put16(2);put16(3);put16(4);put16(w30);put16(6); /* +28h..+32h */
}
static void build_script(void){
    script_size=0;
    put32(1);                                   /* forms */
    put32(2);zeros(20);put32(0x88);zeros(8);put32(1);put("\x03" "abc",4);
    put16(2);zeros(44);                         /* lc_wpns */
    put16(2);                                   /* bases */
    type_record(4,1,1,1,1);
    type_record(2,5,0,0,0);
    stream_bytes=script;
}


static void test_base_types(void){
    uint32_t ecx=0;
    setup();build_script();
    const uint64_t p=es2_base_types_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==87&&observed==87&&slot_seen==39&&stream_at==script_size);
    /* the record set-up and the forms file (call n returns A1h+n, D1h+n, C1h+n) */
    CHECK(calls[0].va==0x49630cu&&calls[0].count==0&&calls[0].eax==VA(0x49766fu)&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x492becu&&calls[1].count==2&&calls[1].arg[0]==VA(0x497712u)&&calls[1].arg[1]==0x7300u&&
        calls[1].eax==0x5555u&&calls[1].edx==0x7300u&&calls[1].ecx==0xe2);
    CHECK(calls[2].va==0x472f14u&&calls[2].count==2&&calls[2].arg[0]==S1&&calls[2].arg[1]==0xa2u&&calls[2].eax==S1&&
        calls[2].edx==0xd2u&&calls[2].ecx==0xa2u&&calls[2].context==8);
    CHECK(is_read(3,S1,4,VA(0x4a95a8u))&&calls[3].eax==S1&&calls[3].edx==TABLE&&calls[3].ecx==0xc3u);
    CHECK(calls[4].va==0x49667cu&&calls[4].count==5&&calls[4].arg[0]==0&&calls[4].arg[1]==0x1cu&&calls[4].arg[2]==1&&
        calls[4].arg[3]==1&&calls[4].arg[4]==VA(0x406a5cu)&&calls[4].eax==0x101u&&calls[4].ecx==1);
    const uint32_t form=HEAP;
    CHECK(r32(0,VA(0x4a95acu))==form);
    CHECK(is_read(5,S1,4,form)&&calls[5].eax==form&&calls[5].edx==S1&&calls[5].ecx==TABLE);
    CHECK(calls[6].va==0x49667cu&&calls[6].arg[1]==0xau&&calls[6].arg[2]==2&&calls[6].arg[4]==VA(0x406a64u)&&calls[6].eax==2);
    const uint32_t elements=HEAP+0x20u;
    CHECK(r32(0,form+4u)==elements);
    CHECK(is_sread(7,S1,20,elements)&&calls[7].eax==20&&calls[7].edx==elements&&calls[7].ecx==S1);
    CHECK(is_read(8,S1,4,form+8u)&&calls[8].eax==form+8u&&calls[8].edx==S1&&calls[8].ecx==TABLE&&r32(0,form+8u)==0x88u);
    CHECK(is_sread(9,S1,8,form+0xcu)&&calls[9].eax==form+0xcu&&calls[9].edx==S1&&calls[9].ecx==0xc9u);
    CHECK(is_read(10,S1,4,form+0x14u)&&calls[10].ecx==TABLE);
    CHECK(calls[11].va==0x4774f0u&&calls[11].count==1&&calls[11].arg[0]==4&&calls[11].eax==4&&calls[11].edx==0xdbu);
    const uint32_t lists=HEAP+0x38u,list0=HEAP+0x40u;
    CHECK(r32(0,form+0x18u)==lists&&r32(0,lists)==list0);
    CHECK(calls[12].va==0x4774f0u&&calls[12].arg[0]==1&&calls[12].eax==1&&calls[12].ecx==4);
    CHECK(is_read(13,S1,1,F-0xe9u)&&calls[13].eax==S1&&calls[13].edx==TABLE&&calls[13].ecx==F-0xe9u);
    CHECK(is_read(14,S1,3,list0)&&calls[14].eax==TABLE&&calls[14].edx==3&&calls[14].ecx==S1);
    CHECK(memcmp(host(0,list0),"abc",3)==0);
    /* stream_destroy: EAX = S, ECX = the loop counter 1, EDX = the last read's */
    CHECK(calls[15].va==0x472f64u&&calls[15].arg[0]==S1&&calls[15].arg[1]==2&&calls[15].eax==S1&&calls[15].ecx==1&&
        calls[15].edx==0xdfu&&r32(0,F-0xe8u)==1);
    /* lc_wpns */
    CHECK(calls[16].va==0x492becu&&calls[16].arg[0]==VA(0x497722u)&&calls[16].arg[1]==0x7300u&&calls[16].edx==0x7300u&&
        calls[16].eax==0xb0u&&calls[16].ecx==0xd0u);
    CHECK(calls[17].va==0x472f14u&&calls[17].arg[0]==S2&&calls[17].arg[1]==0xb1u&&calls[17].ecx==0xb1u&&calls[17].context==0x14);
    CHECK(is_read(18,S2,2,VA(0x4a95b8u))&&calls[18].eax==S2&&calls[18].edx==TABLE);
    CHECK(calls[19].va==0x49667cu&&calls[19].arg[1]==0x16u&&calls[19].arg[2]==2&&calls[19].arg[4]==VA(0x406a6cu)&&calls[19].ecx==2);
    const uint32_t weapons=HEAP+0x48u;
    CHECK(r32(0,VA(0x4a95bcu))==weapons);
    CHECK(is_sread(20,S2,44,weapons)&&calls[20].eax==44&&calls[20].edx==S2&&calls[20].ecx==weapons);
    CHECK(r16(0,F-0xf2u)==44);
    CHECK(calls[21].va==0x472f64u&&calls[21].arg[0]==S2&&calls[21].eax==0x101u&&calls[21].edx==0xe5u&&calls[21].ecx==S2);
    /* bases */
    CHECK(calls[22].va==0x492becu&&calls[22].arg[0]==VA(0x497733u)&&calls[22].eax==0x7300u&&calls[22].edx==0xe6u&&calls[22].ecx==0xd6u);
    CHECK(calls[23].va==0x472f14u&&calls[23].arg[0]==S3&&calls[23].arg[1]==0xb7u&&calls[23].eax==0xb7u&&calls[23].edx==0xb7u&&
        calls[23].ecx==0xd7u&&calls[23].context==0x20);
    CHECK(is_sread(24,S3,2,VA(0x4a9598u))&&calls[24].eax==S3);
    CHECK(calls[25].va==0x49667cu&&calls[25].arg[1]==0x3cu&&calls[25].arg[2]==2&&calls[25].arg[4]==VA(0x406a77u)&&calls[25].edx==2);
    const uint32_t types=HEAP+0x78u,slots=HEAP+0xf4u,children=HEAP+0x104u;
    CHECK(r32(0,VA(0x4a959cu))==types&&r32(0,VA(0x4a95b0u))==slots);
    CHECK(calls[26].va==0x4774f0u&&calls[26].arg[0]==12&&calls[26].eax==types&&calls[26].edx==2&&calls[26].ecx==12);
    for(unsigned k=0;k<5;++k)CHECK(is_read(27+k,S3,2,types+2u*k)&&calls[27+k].eax==types+2u*k&&calls[27+k].edx==TABLE);
    CHECK(calls[27].ecx==0);   /* MOV CX,[f-FAh] over the popped 12 */
    CHECK(r16(0,types+2u)==4&&r16(0,types+6u)==1);
    CHECK(is_sread(32,S3,6,types+0xau)&&calls[32].ecx==types+0xau&&calls[32].eax==0x101u);
    CHECK(is_read(33,S3,2,types+0x10u)&&is_read(34,S3,2,types+0x12u));
    CHECK(calls[35].va==0x49667cu&&calls[35].arg[1]==0x1eu&&calls[35].arg[2]==1&&calls[35].arg[4]==VA(0x406a85u)&&calls[35].ecx==1);
    CHECK(r32(0,types+0x14u)==children);
    CHECK(is_sread(36,S3,30,children)&&calls[36].eax==children&&calls[36].edx==1);
    CHECK(is_sread(37,S3,6,types+0x18u)&&calls[37].edx==1&&calls[37].ecx==types+0x18u);
    CHECK(is_read(38,S3,2,types+0x1eu)&&is_sread(39,S3,4,types+0x20u)&&calls[39].ecx==types+0x20u&&calls[39].eax==0x101u);
    CHECK(is_read(40,S3,2,types+0x24u)&&is_read(41,S3,2,types+0x26u));
    for(unsigned k=0;k<6;++k)CHECK(is_read(42+k,S3,2,types+0x28u+2u*k)&&calls[42+k].eax==types+0x28u+2u*k&&
        calls[42+k].ecx==types+0x28u+2u*k&&r32(0,F-0x100u-4u*k)==types+0x3cu+0x28u+2u*k);
    CHECK(r32(0,types+0x38u)==slots&&r16(0,F-0xfcu)==0x34);
    /* type 1: ECX = the counter word 1 over sx(0) */
    CHECK(is_read(48,S3,2,types+0x3cu)&&calls[48].ecx==1);
    CHECK(calls[56].va==0x49667cu&&calls[56].arg[2]==0&&r32(0,types+0x3cu+0x14u)==HEAP+0x128u);
    CHECK(is_sread(57,S3,6,types+0x3cu+0x18u)&&calls[57].edx==0);
    CHECK(r32(0,types+0x3cu+0x38u)==0&&r16(0,F-0xfau)==2);
    /* basecol: EDX = 0 from type 1's slot clear, ECX = the counter word 2 */
    CHECK(calls[68].va==0x492becu&&calls[68].arg[0]==VA(0x497742u)&&calls[68].eax==0x7300u&&calls[68].edx==0&&calls[68].ecx==2);
    CHECK(calls[69].va==0x472f14u&&calls[69].arg[0]==S4&&calls[69].arg[1]==RA(68)&&calls[69].eax==RA(68)&&calls[69].edx==RA(68)&&
        calls[69].ecx==S4&&calls[69].context==0x2c);
    CHECK(calls[70].va==0x40ccf8u&&calls[70].count==2&&calls[70].arg[0]==slots&&calls[70].arg[1]==S4&&calls[70].eax==S4);
    CHECK(calls[71].va==0x40ccf8u&&calls[71].arg[0]==slots+6u&&calls[71].arg[1]==S4&&calls[71].eax==S4&&calls[71].edx==RD(70));
    CHECK(calls[72].va==0x472f64u&&calls[72].arg[0]==S4&&calls[72].arg[1]==2&&calls[72].edx==S4&&calls[72].eax==RA(71)&&calls[72].ecx==RC(71));
    /* the count words: 4 + 1 = 5 (type 0), 2 + 1 = 3 (type 1), max(1, 5) + 1 = 6 */
    CHECK(r16(0,VA(0x4a9572u))==5&&r16(0,VA(0x4a9578u))==3&&r16(0,VA(0x4a9580u))==6&&r16(0,F-0x11au)==5);
    /* the hulks: after the loop EAX = 2, EDX = types + 2 x 3Ch, ECX = 2 */
    CHECK(calls[73].va==0x492becu&&calls[73].arg[0]==VA(0x497753u)&&calls[73].arg[1]==VA(0x49775au)&&calls[73].eax==2&&
        calls[73].edx==types+0x78u&&calls[73].ecx==2);
    CHECK(calls[74].va==0x474cd8u&&calls[74].count==4&&calls[74].arg[0]==VA(0x4a9584u)&&calls[74].arg[1]==RA(73)&&
        calls[74].arg[2]==0x2bc0001u&&calls[74].arg[3]==0&&calls[74].eax==RA(73)&&r32(0,F-0xf8u)==RA(73));
    CHECK(r16(0,F-0x11cu)==8);
    CHECK(calls[75].va==0x4774f0u&&calls[75].arg[0]==12&&calls[75].eax==0x70008u&&calls[75].edx==12&&calls[75].ecx==0x10bu);
    CHECK(calls[76].va==0x4774f0u&&calls[76].arg[0]==20&&calls[76].eax==20&&calls[76].ecx==12);
    const uint32_t t1=HEAP+0x12cu,t2=HEAP+0x13cu;
    CHECK(r32(0,VA(0x4a957cu))==t1&&r32(0,VA(0x4a9574u))==t2);
    for(unsigned k=0;k<3;++k)CHECK(r32(0,t1+4u*k)==0);
    for(unsigned k=0;k<5;++k)CHECK(r32(0,t2+4u*k)==0);
    CHECK(r32(0,t1+12u)==0xccccccccu&&r32(0,t2+20u)==0xccccccccu);
    /* basetex: after the loops EAX = 5, EDX = 0, ECX = sx(5) */
    CHECK(calls[77].va==0x492becu&&calls[77].arg[0]==VA(0x49775eu)&&calls[77].arg[1]==0x7100u&&calls[77].eax==0x7100u&&
        calls[77].edx==0&&calls[77].ecx==5);
    CHECK(calls[78].va==0x45cdf4u&&calls[78].count==1&&calls[78].arg[0]==RA(77)&&calls[78].eax==RA(77)&&calls[78].edx==RD(77)&&calls[78].ecx==RC(77));
    CHECK(calls[79].va==0x46a03cu&&calls[79].arg[0]==RA(78)&&calls[79].eax==RA(78)&&calls[79].edx==RD(78)&&calls[79].ecx==RA(77));
    CHECK(calls[80].va==0x48349cu&&calls[80].count==2&&calls[80].arg[0]==RA(78)&&calls[80].arg[1]==3&&calls[80].eax==RA(79)&&
        calls[80].edx==OBJ+0x80u&&calls[80].ecx==RA(78));
    CHECK(r32(0,VA(0x4a9588u))==0&&r32(0,VA(0x4a958cu))==RA(79));
    /* vehtex: EAX = 0, EDX = the directory, ECX = 48349c's */
    CHECK(calls[81].va==0x492becu&&calls[81].arg[0]==VA(0x497766u)&&calls[81].arg[1]==0x7100u&&calls[81].eax==0&&
        calls[81].edx==0x7100u&&calls[81].ecx==RC(80));
    CHECK(calls[82].va==0x45cdf4u&&calls[82].arg[0]==RA(81)&&calls[82].eax==RA(81));
    CHECK(calls[83].va==0x46a03cu&&calls[83].arg[0]==RA(82)&&calls[83].eax==RA(82)&&calls[83].edx==RD(82)&&calls[83].ecx==RA(82));
    CHECK(calls[84].va==0x48349cu&&calls[84].arg[0]==RA(82)&&calls[84].arg[1]==3&&calls[84].eax==OBJ+0x80u&&
        calls[84].edx==RA(82)&&calls[84].ecx==RA(82));
    CHECK(r32(0,VA(0x4a9590u))==0&&r32(0,VA(0x4a9594u))==RA(83)&&r32(0,OBJ+0x80u)==43);
    /* +26h of the first six hulks (the count word 4a9580h) */
    for(unsigned k=0;k<6;++k)CHECK(r32(0,SHAPES+0x40u+0x40u*k+0x26u)==VA(0x4a9588u));
    CHECK(r32(0,SHAPES+0x40u+0x40u*6+0x26u)==0xccccccccu);
    /* the debris set: EAX = 6, EDX = 6, ECX = the last hulk */
    CHECK(calls[85].va==0x40874cu&&calls[85].count==3&&calls[85].arg[0]==VA(0x4a95c0u)&&calls[85].arg[1]==VA(0x49776du)&&
        calls[85].arg[2]==0&&calls[85].eax==6&&calls[85].edx==6&&calls[85].ecx==SHAPES+0x180u);
    CHECK(calls[86].va==0x472f64u&&calls[86].arg[0]==S3&&calls[86].arg[1]==2&&calls[86].eax==RA(85)&&calls[86].edx==RD(85));
    CHECK(lo(p)==0x1111u&&hi(p)==RD(86)&&ecx==RC(86));
}

/* A failed open reports through 479c78 with the SETE register = 0 (the
 * status is nonzero) and the path in another register. */
static void test_base_types_report(void){
    uint32_t ecx=0;
    setup();build_script();status_stream=S2;
    (void)es2_base_types_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==88);
    CHECK(calls[17].va==0x472f14u&&calls[17].arg[0]==S2);
    CHECK(calls[18].va==0x479c78u&&calls[18].count==6&&calls[18].arg[0]==VA(0x6c56c8u)&&calls[18].arg[1]==5&&
        calls[18].arg[2]==0&&calls[18].arg[3]==VA(0x49772au)&&calls[18].arg[4]==0x545u&&calls[18].arg[5]==0xb1u&&
        calls[18].eax==S2&&calls[18].edx==0xb1u&&calls[18].ecx==0);
    CHECK(is_read(19,S2,2,VA(0x4a95b8u))&&calls[19].ecx==0xd3u);
    /* the other three: bforms (ECX), bases (EAX), basecol (EDX) */
    setup();build_script();status_stream=S1;
    (void)es2_base_types_registers(&memory,&graph,entry,&ecx);
    CHECK(calls[3].va==0x479c78u&&calls[3].arg[3]==VA(0x497719u)&&calls[3].arg[4]==0x51au&&calls[3].arg[5]==0xa2u&&
        calls[3].eax==S1&&calls[3].edx==0xa2u&&calls[3].ecx==0);
    setup();build_script();status_stream=S3;
    (void)es2_base_types_registers(&memory,&graph,entry,&ecx);
    CHECK(calls[24].va==0x479c78u&&calls[24].arg[3]==VA(0x497739u)&&calls[24].arg[4]==0x550u&&calls[24].arg[5]==0xb7u&&
        calls[24].eax==0&&calls[24].edx==0xe8u&&calls[24].ecx==0xb7u);
    CHECK(is_sread(25,S3,2,VA(0x4a9598u))&&calls[25].eax==RA(24));
    setup();build_script();status_stream=S4;
    (void)es2_base_types_registers(&memory,&graph,entry,&ecx);
    CHECK(calls[70].va==0x479c78u&&calls[70].arg[3]==VA(0x49774au)&&calls[70].arg[4]==0x586u&&calls[70].arg[5]==RA(68)&&
        calls[70].eax==RA(68)&&calls[70].edx==0&&calls[70].ecx==RC(69));
    CHECK(calls[71].va==0x40ccf8u&&calls[71].arg[0]==HEAP+0xf4u);
}

static void test_base_pools(void){
    uint32_t ecx=0;
    setup();context_at=PF-0x18u;
    const uint64_t p=es2_base_pools_registers(&memory,&graph,0x12340009u,0xabcd0000u,entry,&ecx);
    CHECK(call_count==5&&observed==5&&slot_seen==0);
    CHECK(calls[0].va==0x49630cu&&calls[0].count==0&&calls[0].eax==VA(0x497603u)&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x47749cu&&calls[1].count==1&&calls[1].arg[0]==0x12u&&calls[1].eax==0x5555u&&calls[1].edx==0xe1&&calls[1].ecx==0xe2);
    /* pool_create(pool, BX = the count word over the caller's EBX, 26Dh) */
    CHECK(calls[2].va==0x471ad8u&&calls[2].count==3&&calls[2].arg[0]==HEAP&&calls[2].arg[1]==0xabcd0009u&&calls[2].arg[2]==0x26du&&
        calls[2].eax==HEAP&&calls[2].edx==HEAP&&calls[2].ecx==0x12u&&calls[2].context==0x14);
    CHECK(calls[3].va==0x47749cu&&calls[3].arg[0]==8&&calls[3].eax==0xa3u&&calls[3].edx==0xd3u&&calls[3].ecx==HEAP&&calls[3].context==8);
    const uint32_t list=HEAP+0x18u;
    CHECK(calls[4].va==0x4774f0u&&calls[4].arg[0]==100&&calls[4].eax==(list&0xffff0000u)+0x19u&&calls[4].edx==100&&
        calls[4].ecx==8&&calls[4].context==0x20);
    CHECK(r32(0,VA(0x4a95a0u))==HEAP&&r32(0,VA(0x4a95a4u))==list&&r32(0,list)==HEAP+0x24u&&r16(0,list+4u)==0&&
        r16(0,list+6u)==25&&r16(0,PF-0x18u)==8&&r32(0,PF-4u)==HEAP&&r32(0,PF-8u)==list);
    CHECK(lo(p)==0x2222u&&hi(p)==list&&ecx==list);
    /* both new fail: no pool_create, no new[]; EDX = 0, ECX = 8 */
    setup();context_at=PF-0x18u;new_fails=2;
    const uint64_t q=es2_base_pools_registers(&memory,&graph,5,0,entry,&ecx);
    CHECK(call_count==3&&calls[1].va==0x47749cu&&calls[2].va==0x47749cu&&calls[2].eax==0&&calls[2].ecx==0&&calls[2].context==0xccccu);
    CHECK(r32(0,VA(0x4a95a0u))==0&&r32(0,VA(0x4a95a4u))==0);
    CHECK(lo(q)==0x2222u&&hi(q)==0&&ecx==8);
}

int main(void){
    test_base_types();
    test_base_types_report();
    test_base_pools();
    printf("building_types: %u checks\n",checks);
    return 0;
}
