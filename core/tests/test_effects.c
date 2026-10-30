#include "es2/core/effects.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Guest addresses deliberately differ from host addresses, also on x64. */
enum { BASE=0x12340000, A=BASE+0x100, C=BASE+0x500, T=BASE+0x700,
       M=BASE+0x800, L=BASE+0x900, V=BASE+0xa00, S=BASE+0xb00,
       B=BASE+0xc00, ENTRY=BASE+0xe00, RECORDS=BASE+0xf00,
       DEFAULT=BASE+0x1100, REF_SLOT=BASE+0x1200, REF=BASE+0x1300,
       POINT=BASE+0x1400 };
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static unsigned char arena[0x1800];
static void *host(void *c,uint32_t a) { (void)c; CHECK(a>=BASE && a-BASE<sizeof(arena)); return arena+(a-BASE); }
static uint16_t r16(void *c,uint32_t a) { unsigned char *p=host(c,a); return (uint16_t)(p[0]|p[1]<<8); }
static uint32_t r32(void *c,uint32_t a) { return (uint32_t)r16(c,a)|((uint32_t)r16(c,a+2)<<16); }
static void w16(void *c,uint32_t a,uint16_t v) { unsigned char *p=host(c,a); p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void w32(void *c,uint32_t a,uint32_t v) { w16(c,a,(uint16_t)v);w16(c,a+2,(uint16_t)(v>>16)); }
static es2_guest_memory memory={0,r32,r16,w32,w16,host};
static unsigned ids[128],used,observed;
static uint32_t distance_value,lookup_value,selector_value,lookup_record;
static int toggle_sound,second_lookup_changes_special;
static unsigned lookups;
static uint64_t callback(void *c,uint32_t f,const uint32_t *x,unsigned n,const uint32_t *registers) {
    (void)c;CHECK(used<128);ids[used++]=f;
    switch(f) {
    case 6:
        CHECK(n==2 && x[0]==L && x[1]==selector_value);
        if(++lookups==2 && second_lookup_changes_special)w16(0,C+0x46,9);
        return ((uint64_t)0xabcdef01u<<32)|lookup_value;
    case 1:
        CHECK(n==2 && x[0]==A+0xc && x[1]==A+0x12);
        CHECK(r16(0,A+0x32)==0);w32(0,A+0x12,0x11223344);return 0;
    case 2:
        CHECK(n==3 && x[0]==lookup_record+0x14 && x[1]==A+0x12 && x[2]==POINT);
        CHECK(r16(0,A+0x32)!=0);
        w32(0,POINT,101);w32(0,POINT+4,202);w32(0,POINT+8,303);return UINT64_MAX;
    case 3:
        CHECK(n==3 && x[0]==0xffffffffu && x[1]==0x80000000u && x[2]==0x7fffffffu);
        CHECK(r32(0,B+0x26)==101 && r32(0,B+0x2a)==202 && r32(0,B+0x2e)==303);
        CHECK(r16(0,B+0x10)==0xabcd && r16(0,B+0x32)==0);
        return ((uint64_t)0xa5a5a5a5u<<32)|distance_value;
    case 4:
        CHECK(n==2 && x[0]==29 && x[1]==A+0x26);
        CHECK(*(unsigned char *)host(0,A+0x2b4)!=0);
        if(toggle_sound) *(unsigned char *)host(0,A+0xa3)^=1;
        return UINT64_C(0xdeadbeefbad0cafe);
    case 5:
        CHECK(n==0 && registers && registers[0]==A+0x92 && registers[1]==A+0x92);CHECK(*(unsigned char *)host(0,A+0x2b4)!=0);
        return UINT64_C(0xcafebabe87654321);
    default: CHECK(0);return 0;
    }
}
static void observer(void *c,es2_effects_event event,uint32_t key) {
    (void)c;CHECK(observed==used);CHECK((event==ES2_EFFECTS_SLOT && key==0x20) || (event==ES2_EFFECTS_CALL && key>=1 && key<=5));++observed;
}
static es2_effects_context g={REF_SLOT,DEFAULT,POINT,1,2,3,4,5,callback,0,observer,0};
static void reset(void) {
    memset(arena,0,sizeof(arena)); used=observed=0;toggle_sound=second_lookup_changes_special=0;lookups=0;
    w32(0,A+0x22c,T);w32(0,A+0x1f2,C);w32(0,A+0x34,M);w32(0,A+0x238,S);w16(0,A+0x23c,1);
    w32(0,S,B);w32(0,M+4,L);w32(0,L,V);w32(0,V+0x20,6);w32(0,M+0x16,RECORDS);
    w16(0,ENTRY+4,0);w16(0,T+0x16,10);w16(0,T+4,10);w16(0,A+0x10,0xabcd);w16(0,A+0x2a0,1);
    w16(0,C+0x46,9);w16(0,C+0xe,10);w16(0,C+0x7c,11);w16(0,C+0x12,12);w16(0,C+0x14,13);w16(0,C+0x10,14);
    *(unsigned char *)host(0,C+0x77)=0x80;selector_value=(C&0xffff0000u)|0xff80u;
    w32(0,REF_SLOT,REF);w32(0,A+0x26,0xffffffffu);w32(0,A+0x2a,0x80000000u);w32(0,A+0x2e,0x7fffffffu);
    distance_value=14999;lookup_value=ENTRY;lookup_record=RECORDS;
}
static uint64_t run(void) {
    uint64_t r=es2_ground_effects_registers(&memory,A,&g,0x76543210u,0x89abcdefu);
    CHECK(used==observed);return r;
}
static unsigned calls(unsigned id) { unsigned n=0;for(unsigned i=0;i<used;++i)n+=ids[i]==id;return n; }
int main(void) {
    reset();w32(0,A+0x22c,0);CHECK(run()==UINT64_C(0x89abcdef76543210) && used==0);
    for(unsigned i=0;i<2;++i) {
        reset();w16(0,A+0x23c,i?0x8000:0);
        CHECK(run()==(((uint64_t)(C+0x72)<<32)|((A+0x2b4)&0xffff0000u)));CHECK(used==0);
    }
    reset();w32(0,S,0);CHECK(run()==UINT64_C(0x0000000012340001));CHECK(!used);
    for(unsigned cache=0;cache<2;++cache) for(unsigned lookup=0;lookup<3;++lookup) {
        reset();w16(0,A+0x32,(uint16_t)cache);
        if(lookup==1) {lookup_value=0;lookup_record=DEFAULT;}
        if(lookup==2) {w16(0,ENTRY+4,0xffff);lookup_record=DEFAULT;}
        (void)run();CHECK(ids[0]==6 && ids[1]==(cache?2:1));CHECK(calls(1)==!cache);
    }
    /* Explicit strict-boundary table; forward/reverse arm and fire ties,
     * zero rate, signed threshold extremes, pre-armed and clear latches. */
    static const struct { int rate,coord,arm,fire,latch,expected,plays; } cases[]={
        {1,0,-1,1,0,0,1},{1,0,0,1,0,0,0},{1,0,1,1,0,0,0},
        {1,0,-1,0,0,1,0},{1,0,-1,-1,0,1,0},{1,0,1,1,7,0,1},
        {0,0,1,-1,0,0,1},{-1,0,0,-1,0,0,0},{-1,0,-1,-1,0,0,0},
        {-1,0,1,0,0,1,0},{-1,0,1,1,0,1,0},{-32768,0,-1,-1,2,0,1},
        {32767,32767,-32768,32767,0,1,0},{1,-32768,-32768,32767,1,0,1},
    };
    for(unsigned id=10;id<=14;++id) for(unsigned j=0;j<sizeof(cases)/sizeof(cases[0]);++j) {
        if(id==14 && cases[j].rate<=0)continue; /* k=2 always forward */
        for(unsigned near=0;near<3;++near) for(unsigned flag=0;flag<2;++flag) {
            reset();w16(0,T+0x16,(uint16_t)id);w16(0,A+0x2a0,(uint16_t)cases[j].rate);
            unsigned k=id==14?2:cases[j].rate>0?0:1;
            w32(0,RECORDS+0x18,(uint32_t)cases[j].coord);
            w16(0,C+0xac+2*k,(uint16_t)cases[j].arm);w16(0,C+0x98+2*k,(uint16_t)cases[j].fire);
            *(unsigned char *)host(0,A+0x2b4)=(unsigned char)cases[j].latch;
            *(unsigned char *)host(0,A+0xa3)=(unsigned char)flag;
            distance_value=near==0?14999:near==1?15000:0x80000000u;
            uint64_t r=run();unsigned enabled=near!=1||flag,fire=enabled&&cases[j].plays;
            CHECK(calls(4)==(fire&&near!=1));CHECK(calls(5)==(fire&&flag));
            CHECK(*(unsigned char *)host(0,A+0x2b4)==(enabled?cases[j].expected:cases[j].latch));
            CHECK((uint16_t)r==1);
            if(fire) CHECK(r==(flag?UINT64_C(0xcafebabe87650001):((uint64_t)(A+0x92)<<32)|0x12340001u));
        }
    }
    for(unsigned special=0;special<2;++special) for(unsigned category=0;category<3;++category) {
        reset();w16(0,T+0x16,(uint16_t)(special?9:99));*(unsigned char *)host(0,C+0x72)=(unsigned char)category;
        CHECK(run()==UINT64_C(0x00000001ffff0001));CHECK(!calls(4)&&!calls(5));
    }
    reset();w16(0,T+0x16,0xffff);w16(0,C+0xac,0xffff);w16(0,C+0x98,1);toggle_sound=1;
    CHECK(run()==UINT64_C(0xcafebabe87650001));CHECK(calls(4)==1&&calls(5)==1);
    /* A null slot does not consume a latch; the following attachment fires
     * latch zero, even though its slot and configuration index are one. */
    reset();w16(0,A+0x23c,2);w32(0,S,0);w32(0,S+4,B);
    *(unsigned char *)host(0,C+0x78)=0x80;w16(0,C+0xac,0xffff);w16(0,C+0x98,1);
    *(unsigned char *)host(0,A+0x2b5)=0x55;(void)run();
    CHECK(calls(4)==1 && *(unsigned char *)host(0,A+0x2b4)==0 && *(unsigned char *)host(0,A+0x2b5)==0x55);
    /* The disabled special animation still redirects F, then advances it.
     * A lookup mutation makes the next iteration ordinary and arms +2b9. */
    reset();w16(0,A+0x23c,2);w32(0,S+4,B);w16(0,C+0x46,10);
    *(unsigned char *)host(0,C+0x72)=2;*(unsigned char *)host(0,C+0x78)=0x80;
    w16(0,C+0xac,0xffff);w16(0,C+0x98,0xffff);second_lookup_changes_special=1;(void)run();
    CHECK(*(unsigned char *)host(0,A+0x2b4)==0 && *(unsigned char *)host(0,A+0x2b8)==0 && *(unsigned char *)host(0,A+0x2b9)==1);
    printf("PASS: effects synthetic contract, %u checks; all 30 branch predicates, callbacks and mapped guest scratch\n",checks);
    return 0;
}
