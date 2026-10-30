#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/damage.h"
/* Synthetic state for the core (damage data and part tables). The
 * image base is placed so that the globals 498854..498864, 4a9828 and
 * 6cbee4 fall inside the arena. A logging call function stands in for every
 * game call (target, arguments, count, entry EAX/EDX/ECX): new/new[] return
 * the next block of a bump area, memset/memcpy fill and copy, the read
 * method copies from a scripted byte stream, and every other call returns
 * EAX = A0h+n, EDX = D0h+n, ECX = C0h+n (n = call number). Expected values
 * are worked out by hand from the contract in mission_load.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x98000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define HEAP (BASE+0x300000u)
#define OBJ (BASE+0x2f0000u)
#define READ_METHOD UINT32_C(0x77700000)
static uint8_t arena[0x310000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"damage_data line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct call { uint32_t va,arg[6],count,eax,edx,ecx; } call;
static call calls[64];static unsigned call_count,observed,slot_seen;
static uint32_t heap_next;static int new_fails;
static const uint8_t *stream_bytes;static unsigned stream_at;
static uint32_t va_of(uint32_t f){return f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<64);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=va_of(f);for(unsigned i=0;i<n&&i<6;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    const uint32_t eax=0xa0u+call_count,edx=0xd0u+call_count,ecx=0xc0u+call_count;
    if(k->va==0x4774f0u||k->va==0x47749cu){
        if(new_fails){r->eax=0;}else{r->eax=heap_next;heap_next+=(a[0]+7u)&~3u;}
        r->edx=edx;r->ecx=ecx;return;
    }
    if(k->va==0x49681au){for(uint32_t i=0;i<a[2];++i)((uint8_t *)host(0,a[0]+i))[0]=(uint8_t)a[1];r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    if(k->va==0x49680eu){for(uint32_t i=0;i<a[2];++i)((uint8_t *)host(0,a[0]+i))[0]=((uint8_t *)host(0,a[1]+i))[0];r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    if(f==READ_METHOD){for(uint32_t i=0;i<a[1];++i)((uint8_t *)host(0,a[2]+i))[0]=stream_bytes[stream_at++];r->eax=a[1];r->edx=edx;r->ecx=ecx;return;}
    if(k->va==0x49630cu){r->eax=0x5555u;return;}   /* the record stub: EAX = the head, EDX/ECX kept */
    r->eax=eax;r->edx=edx;r->ecx=ecx;
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;if(slot)++slot_seen;++observed;}
static es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(void){memset(arena,0xcc,sizeof(arena));call_count=observed=slot_seen=0;heap_next=HEAP;new_fails=0;stream_at=0;}
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}
/* A stream object at OBJ whose table's slot +18h is READ_METHOD. */
#define STREAM (OBJ+0x100u)
#define TABLE (OBJ+0x200u)
static void make_stream(void){w32(0,STREAM,TABLE);w32(0,TABLE+0x18u,READ_METHOD);}

static void test_region_read(void){
    uint32_t ecx=0;
    /* Two links: prefix (8 bytes, word +6 = 2), then 8 link bytes. */
    static const uint8_t bytes[]={1,2,3,4,5,6,2,0, 9,8,7,6,5,4,3,2};
    setup();make_stream();stream_bytes=bytes;
    const uint32_t R=OBJ+0x400u;
    uint64_t p=es2_dmg_region_read_registers(&memory,&graph,R,STREAM,entry,&ecx);
    CHECK(call_count==3&&observed==3&&slot_seen==2);
    CHECK(calls[0].va==READ_METHOD&&calls[0].arg[0]==STREAM&&calls[0].arg[1]==8&&calls[0].arg[2]==R&&calls[0].eax==TABLE&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x4774f0u&&calls[1].arg[0]==8&&calls[1].eax==8&&calls[1].edx==0xd1u&&calls[1].ecx==0xc1u);
    CHECK(calls[2].arg[1]==8&&calls[2].arg[2]==HEAP&&calls[2].eax==TABLE&&calls[2].edx==HEAP&&calls[2].ecx==8);
    CHECK(r32(0,R+8)==HEAP&&r32(0,HEAP)==0x06070809u&&r16(0,R+0xc)==0xffffu&&r32(0,R+0xe)==0&&r16(0,R+6)==2);
    CHECK(lo(p)==8&&hi(p)==0&&ecx==0xc3u);
    /* No links: one read; EAX/ECX the read's, EDX 0; R+8 untouched. */
    static const uint8_t none[]={1,2,3,4,5,6,0,0};
    setup();make_stream();stream_bytes=none;
    p=es2_dmg_region_read_registers(&memory,&graph,R,STREAM,entry,&ecx);
    CHECK(call_count==1&&r32(0,R+8)==0xccccccccu&&r16(0,R+0xc)==0xffffu&&lo(p)==8&&hi(p)==0&&ecx==0xc1u);
}

static void test_table_read(void){
    uint32_t ecx=0;
    /* Two capacities (5, 7), one region without links. */
    static const uint8_t bytes[]={2,0, 5,0,7,0, 1,0, 0x10,0,0,0,0,0,0,0};
    setup();make_stream();stream_bytes=bytes;
    const uint32_t T=OBJ+0x600u;
    uint64_t p=es2_dmg_read_registers(&memory,&graph,T,STREAM,entry,&ecx);
    /* read, new[], read, read, new[], 40cff8 (a logged call: the region
     * reader is a game call) */
    CHECK(call_count==6&&slot_seen==3);
    CHECK(calls[0].va==READ_METHOD&&calls[0].arg[1]==2&&calls[0].arg[2]==T+0xa&&calls[0].eax==T+0xa&&calls[0].edx==STREAM&&calls[0].ecx==TABLE);
    CHECK(calls[1].va==0x4774f0u&&calls[1].arg[0]==4&&calls[1].eax==4);
    CHECK(calls[2].arg[1]==4&&calls[2].arg[2]==HEAP&&calls[2].eax==4&&calls[2].edx==STREAM&&calls[2].ecx==TABLE);
    CHECK(calls[3].arg[1]==2&&calls[3].arg[2]==T+8&&calls[3].eax==T+8);
    CHECK(calls[4].va==0x4774f0u&&calls[4].arg[0]==18&&calls[4].eax==1&&calls[4].edx==18);
    CHECK(calls[5].va==0x40cff8u&&calls[5].count==2&&calls[5].arg[0]==HEAP+8&&calls[5].arg[1]==STREAM&&calls[5].eax==STREAM&&calls[5].ecx==18);
    CHECK(r32(0,T+4)==HEAP&&r32(0,T)==HEAP+8&&r16(0,T+8)==1&&r16(0,T+0xa)==2&&r16(0,HEAP)==5&&r16(0,HEAP+2)==7);
    CHECK(lo(p)==0xa6u&&hi(p)==0xd6u&&ecx==0xc6u);
    /* No region: EAX = the region block, EDX = new[]'s, ECX = 0. */
    static const uint8_t empty[]={0,0, 0,0};
    setup();make_stream();stream_bytes=empty;
    p=es2_dmg_read_registers(&memory,&graph,T,STREAM,entry,&ecx);
    CHECK(call_count==5&&calls[1].arg[0]==0&&calls[4].arg[0]==0&&lo(p)==HEAP+4&&hi(p)==0xd5u&&ecx==0);
}

static void test_load(void){
    uint32_t ecx=0;
    setup();
    const uint32_t S=OBJ+0x1000u,f=S+ES2_DMG_LOAD_FRAME;
    graph.scratch=S;
    w16(0,VA(0x498854u),0x0003u);w32(0,VA(0x4a9828u),OBJ+0x2000u);
    w32(0,f-0x2cu,0);                               /* the stream opened */
    w32(0,f-0x50u,0x1111u);
    uint64_t p=es2_dmg_load_registers(&memory,&graph,0x5000u,entry,&ecx);
    CHECK(call_count==4);
    CHECK(calls[0].va==0x49630cu&&calls[0].eax==VA(0x498840u)&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x472f14u&&calls[1].arg[0]==f-0x30u&&calls[1].arg[1]==0x5000u&&calls[1].eax==0x5555u&&calls[1].edx==f-0x30u);
    const uint32_t slot=OBJ+0x2000u+36u;
    CHECK(calls[2].va==0x40d09cu&&calls[2].arg[0]==slot&&calls[2].arg[1]==f-0x30u&&calls[2].eax==f-0x30u&&calls[2].edx==0xd2u&&calls[2].ecx==3);
    CHECK(calls[3].va==0x472f64u&&calls[3].arg[0]==f-0x30u&&calls[3].arg[1]==2&&calls[3].eax==slot&&calls[3].edx==f-0x30u&&calls[3].ecx==0xc3u);
    CHECK(r16(0,f-0x40u)==8&&r16(0,VA(0x498854u))==4&&lo(p)==slot&&hi(p)==0x1111u&&ecx==0xc4u);
    /* The open failed (+4 nonzero): the error report with EDX = 0, EAX =
     * the state; a negative counter word: slot below the array. */
    setup();
    w16(0,VA(0x498854u),0xffffu);w32(0,VA(0x4a9828u),OBJ+0x2000u);
    w32(0,f-0x2cu,7);w32(0,f-0x50u,0x2222u);
    p=es2_dmg_load_registers(&memory,&graph,0x5000u,entry,&ecx);
    CHECK(call_count==5&&calls[2].va==0x479c78u&&calls[2].count==6&&calls[2].arg[0]==VA(0x6c56c8u)&&calls[2].arg[1]==5);
    CHECK(calls[2].arg[2]==0&&calls[2].arg[3]==VA(0x498858u)&&calls[2].arg[4]==0x85u&&calls[2].arg[5]==0x5000u&&calls[2].eax==7&&calls[2].edx==0);
    CHECK(calls[3].arg[0]==OBJ+0x2000u-12u&&calls[3].ecx==0xffffu);
    CHECK(r16(0,VA(0x498854u))==0&&lo(p)==OBJ+0x2000u-12u&&hi(p)==0x2222u);
    graph.scratch=0;
}

static void test_region_copy_and_set(void){
    uint32_t ecx=0;
    setup();
    const uint32_t R=OBJ+0x3000u,D=OBJ+0x3100u,L=OBJ+0x3200u;
    for(unsigned i=0;i<18;++i)((uint8_t *)host(0,R+i))[0]=(uint8_t)(0x40+i);
    w16(0,R+6,3);w32(0,R+8,L);for(unsigned i=0;i<12;++i)((uint8_t *)host(0,L+i))[0]=(uint8_t)(0x90+i);
    uint64_t p=es2_dmg_region_copy_registers(&memory,&graph,D,R,entry,&ecx);
    CHECK(call_count==3&&calls[0].va==0x49680eu&&calls[0].arg[0]==D&&calls[0].arg[1]==R&&calls[0].arg[2]==0x12&&calls[0].eax==0xe0&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x4774f0u&&calls[1].arg[0]==12&&calls[1].eax==12&&calls[1].edx==0x12&&calls[1].ecx==0);
    CHECK(calls[2].arg[0]==HEAP&&calls[2].arg[1]==L&&calls[2].arg[2]==12&&calls[2].eax==12&&calls[2].edx==L&&calls[2].ecx==12);
    CHECK(r32(0,D+8)==HEAP&&r16(0,D+6)==3&&r32(0,HEAP+8)==0x9b9a9998u&&lo(p)==HEAP&&hi(p)==12&&ecx==0);
    /* region_set: two regions by index (2 and -1) from two sources; the
     * region copy is a logged game call. */
    setup();
    const uint32_t T=OBJ+0x4000u,I=OBJ+0x4100u,P=OBJ+0x4200u,ROWS=OBJ+0x5000u;
    w32(0,T,ROWS);w16(0,I,2);w16(0,I+2,0xffff);w32(0,P,0x111);w32(0,P+4,0x222);
    p=es2_dmg_region_set_registers(&memory,&graph,T,I,P,2,entry,&ecx);
    CHECK(call_count==2&&calls[0].va==0x40d050u&&calls[0].arg[0]==ROWS+36&&calls[0].arg[1]==0x111&&calls[0].eax==2&&calls[0].edx==36&&calls[0].ecx==ROWS+36);
    CHECK(calls[1].arg[0]==ROWS-18&&calls[1].arg[1]==0x222&&calls[1].eax==0xffffffffu&&calls[1].edx==(uint32_t)-18&&lo(p)==0xa2u&&hi(p)==0xd2u&&ecx==0xc2u);
    /* n = 0: no call, the entry registers. */
    setup();p=es2_dmg_region_set_registers(&memory,&graph,T,I,P,0x10000u,entry,&ecx);
    CHECK(call_count==0&&lo(p)==0xe0&&hi(p)==0xe1&&ecx==0xe2);
}

static void test_capacity_and_parts(void){
    uint32_t ecx=0;
    setup();
    const uint32_t T=OBJ+0x6000u,C=OBJ+0x6100u,I=OBJ+0x6200u,V=OBJ+0x6300u;
    w32(0,T+4,C);w16(0,I,3);w16(0,I+2,0xfffe);w16(0,V,0x1234);w16(0,V+2,0x5678);
    uint64_t p=es2_dmg_capacity_set_registers(&memory,T,I,V,0xabcd0002u,entry,&ecx);
    CHECK(r16(0,C+6)==0x1234&&r16(0,C-4)==0x5678&&lo(p)==2&&hi(p)==V+4&&ecx==I+4);
    /* The count is a signed word: FFFFh writes nothing. */
    p=es2_dmg_capacity_set_registers(&memory,T,I,V,0xffffu,entry,&ecx);
    CHECK(lo(p)==0&&hi(p)==V&&ecx==I);
    /* An index list read after the previous value was written: the
     * capacity array overlaps the index list. */
    setup();w32(0,T+4,I);w16(0,I,1);w16(0,I+2,0);w16(0,V,0x0002);w16(0,V+2,0x7777);
    p=es2_dmg_capacity_set_registers(&memory,T,I,V,2,entry,&ecx);
    CHECK(r16(0,I+2)==0x0002&&r16(0,I+4)==0x7777&&lo(p)==2);
    /* part_store: rows of 18 bytes, word +0Ch and dword +0Eh. */
    setup();
    const uint32_t ROWS=OBJ+0x7000u,W=OBJ+0x6400u,Q=OBJ+0x6500u;
    w32(0,T,ROWS);w16(0,I,1);w16(0,I+2,4);w16(0,W,0x11);w16(0,W+2,0x22);w32(0,Q,0xaaaa);w32(0,Q+4,0xbbbb);
    p=es2_dmg_part_store_registers(&memory,T,2,I,W,Q,entry,&ecx);
    CHECK(r16(0,ROWS+18+0xc)==0x11&&r32(0,ROWS+18+0xe)==0xaaaa&&r16(0,ROWS+72+0xc)==0x22&&r32(0,ROWS+72+0xe)==0xbbbb);
    CHECK(lo(p)==ROWS+72&&hi(p)==2&&ecx==0xbbbb);
    p=es2_dmg_part_store_registers(&memory,T,0,I,W,Q,entry,&ecx);
    CHECK(lo(p)==0xe0&&hi(p)==0&&ecx==0xe2);
}

static void test_col_parts(void){
    uint32_t ecx=0;
    setup();
    /* A record: owner 0x0042, two entries at E (ids 7, 9). */
    const uint32_t C=OBJ+0x8000u,E=OBJ+0x8ffcu,I=OBJ+0x9100u,O=OBJ+0x9200u,P=OBJ+0x9300u;
    w16(0,C,0x42);w16(0,C+4,2);w32(0,C+6,E);w16(0,E+6,7);w16(0,E+0x16,9);
    uint64_t p=es2_col_part_gather_registers(&memory,C,I,O,P,entry,&ecx);
    CHECK(r16(0,I)==7&&r16(0,I+2)==9&&r16(0,O)==0x42&&r16(0,O+2)==0x42&&r32(0,P)==E+8&&r32(0,P+4)==E+0x18);
    /* EAX: the high word of E + 20h (carried into the high word), low word
     * the count. */
    CHECK(lo(p)==(((E+0x20u)&0xffff0000u)|2u)&&hi(p)==C&&ecx==2);
    /* The ids overlap the owner word: owner[0] is the id just written. */
    setup();w16(0,C,0x42);w16(0,C+4,1);w32(0,C+6,E);w16(0,E+6,0x99);
    p=es2_col_part_gather_registers(&memory,C,C,O,P,entry,&ecx);
    CHECK(r16(0,O)==0x99&&ecx==1);
    /* collect: two records; col_part_gather a logged call returning
     * EAX = A0h+n, so b = A1h, then A1h+A2h. */
    setup();
    const uint32_t L=OBJ+0xa000u,RECS=OBJ+0xa100u;
    w16(0,L,2);w32(0,L+2,RECS);
    p=es2_col_part_collect_registers(&memory,&graph,L,I,O,P,entry,&ecx);
    CHECK(call_count==2&&calls[0].va==0x40cafcu&&calls[0].arg[0]==RECS&&calls[0].arg[1]==I&&calls[0].arg[2]==O&&calls[0].arg[3]==P);
    CHECK(calls[0].eax==P&&calls[0].edx==O&&calls[0].ecx==I);
    CHECK(calls[1].arg[0]==RECS+10&&calls[1].arg[1]==I+2*0xa1u&&calls[1].arg[2]==O+2*0xa1u&&calls[1].arg[3]==P+4*0xa1u);
    CHECK(lo(p)==0xa1u+0xa2u&&hi(p)==0xd2u&&ecx==0xc2u);
    setup();w16(0,L,0);
    p=es2_col_part_collect_registers(&memory,&graph,L,I,O,P,entry,&ecx);
    CHECK(call_count==0&&lo(p)==0&&hi(p)==0xe1&&ecx==0xe2);
}

static void test_state(void){
    uint32_t ecx=0;
    setup();
    const uint32_t D=OBJ+0xb000u;
    uint64_t p=es2_damage_state_ctor_registers(&memory,&graph,D,0x5555001du,0x66660016u,entry,&ecx);
    CHECK(call_count==6&&calls[0].va==0x4774f0u&&calls[0].arg[0]==0x2c&&calls[0].eax==0x2c&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].arg[0]==0x3a&&calls[1].eax==HEAP&&calls[1].edx==0x3a&&calls[1].ecx==0x2c);
    CHECK(calls[2].arg[0]==0x3a&&calls[2].edx==0xd2u&&calls[2].ecx==0x3a);
    CHECK(calls[3].va==0x49681au&&calls[3].arg[1]==0&&calls[3].arg[2]==0x2c&&calls[3].eax==0x2c&&calls[3].edx==HEAP&&calls[3].ecx==0x3a);
    CHECK(calls[4].arg[1]==0&&calls[4].arg[2]==0x3a&&calls[4].eax==r32(0,D+4)&&calls[4].edx==0x2c&&calls[4].ecx==0x3a);
    CHECK(calls[5].arg[1]==1&&calls[5].arg[2]==0x3a&&calls[5].eax==r32(0,D+4)&&calls[5].edx==0x3a&&calls[5].ecx==r32(0,D+8));
    CHECK(r16(0,D+0x18)==0x1d&&r16(0,D+0x1a)==0x16&&r16(0,r32(0,D+8))==0x0101u&&r16(0,r32(0,D+4))==0);
    CHECK(lo(p)==D&&hi(p)==0x3a&&ecx==0);
    /* bind: the shared block once. */
    setup();
    const uint32_t S=OBJ+0xc000u,f=S+ES2_DAMAGE_BIND_FRAME;graph.scratch=S;
    w32(0,VA(0x498864u),0);w32(0,VA(0x6cbee4u),OBJ+0xc100u);w32(0,OBJ+0xc100u,10);w32(0,f-0x24u,0x3333u);
    p=es2_damage_state_bind_registers(&memory,&graph,D,0xa,0xd,0xc,entry,&ecx);
    CHECK(call_count==3&&calls[0].va==0x49630cu&&calls[0].eax==VA(0x498878u));
    CHECK(calls[1].va==0x47749cu&&calls[1].arg[0]==6&&calls[1].eax==0xa&&calls[1].edx==0xd&&calls[1].ecx==0xc);
    CHECK(calls[2].va==0x4774f0u&&calls[2].arg[0]==0x28&&calls[2].eax==0x28&&calls[2].edx==HEAP&&calls[2].ecx==0x14);
    CHECK(r32(0,D+0xc)==0xd&&r32(0,D+0x10)==0xc&&r32(0,D+0x14)==0xa&&r32(0,VA(0x498864u))==HEAP);
    CHECK(r32(0,HEAP)==HEAP+12&&((uint8_t *)host(0,HEAP+4))[0]==0x14&&((uint8_t *)host(0,HEAP+5))[0]==0&&r32(0,OBJ+0xc100u)==10&&r16(0,f-0x14u)==8);
    CHECK(lo(p)==HEAP&&hi(p)==0x3333u&&ecx==OBJ+0xc100u);
    /* Present: EAX = A, ECX = C. */
    call_count=0;p=es2_damage_state_bind_registers(&memory,&graph,D,0xa,0xd,0xc,entry,&ecx);
    CHECK(call_count==1&&lo(p)==0xa&&ecx==0xc);
    /* new fails: [498864h] stays 0, EAX = 0, ECX = 6. */
    setup();w32(0,VA(0x498864u),0);w32(0,f-0x24u,0x4444u);new_fails=1;
    p=es2_damage_state_bind_registers(&memory,&graph,D,0xa,0xd,0xc,entry,&ecx);
    CHECK(call_count==2&&r32(0,VA(0x498864u))==0&&lo(p)==0&&hi(p)==0x4444u&&ecx==6);
    graph.scratch=0;
}

int main(void){
    test_region_read();
    test_table_read();
    test_load();
    test_region_copy_and_set();
    test_capacity_and_parts();
    test_col_parts();
    test_state();
    printf("damage_data: %u checks passed\n",checks);
    return 0;
}
