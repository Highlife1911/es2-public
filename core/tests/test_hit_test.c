#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/hit_test.h"
/* Synthetic guest: actor, vtable, packet, segment, configuration, tables
 * at 90000000h, an image base I with the globals the hit tests read, a
 * frame scratch at A0000000h. Every game call is logged with its arguments
 * and entry registers; the stubs answer from a script and write planned
 * outputs. The expectations below are read from the reference listing
 * (hit_tests.md, "hit tests"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_HIT_TEST_FRAME-(x))
#define HEAD UINT32_C(0x0012ff00)
static uint8_t arena[0x1000],scratch[ES2_HIT_TEST_SCRATCH],g1[0x40],g2[0x10],g3[0x10],g4[0x10];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"hit_test line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4a95f0)<sizeof(g1))return g1+(a-G(0x4a95f0));
    if(a-G(0x6cbee0)<sizeof(g2))return g2+(a-G(0x6cbee0));
    if(a-G(0x49ef30)<sizeof(g3))return g3+(a-G(0x49ef30));
    if(a-G(0x4d2680)<sizeof(g4))return g4+(a-G(0x4d2680));
    fprintf(stderr,"hit_test: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
#define ACT (OBJ+0x080)
#define VT (OBJ+0x340)
#define PK (OBJ+0x3c0)
#define SEG (OBJ+0x3e0)
#define POSE (OBJ+0x420)
#define CONF (OBJ+0x440)
#define PARTS (OBJ+0x480)
#define TABLE (OBJ+0x490)
#define HIT (OBJ+0x4b0)
#define REC (OBJ+0x4c0)
#define COUNTER (OBJ+0x4d0)
#define EXPL (OBJ+0x4e0)
#define SLOT10 UINT32_C(0x55500010)
#define SLOT74 UINT32_C(0x55500074)
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; } logged;
typedef struct answer { uint32_t eax,edx,ecx; } answer;
static logged log_[32];static unsigned calls;
static answer script[32];static unsigned answers;
static uint32_t out_word,out_dword;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49630c)){CHECK(n==0);w32(0,F(0x24),HEAD);r->eax=HEAD;return;}
    CHECK(answers<32);
    if(f==G(0x427da8))w32(0,a[0],out_dword);           /* object_query: the length */
    else if(f==G(0x413cc4))w16(0,a[4],(uint16_t)out_word); /* shield_geometry: damage left */
    else if(f==G(0x48043c))for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,0x700+i);
    else if(f==G(0x47fa20))memset(host(0,a[2]),0x33,32);
    r->eax=script[answers].eax;r->edx=script[answers].edx;r->ecx=script[answers].ecx;++answers;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0,sizeof(arena));memset(scratch,0x5a,sizeof(scratch));
    memset(g1,0,sizeof(g1));memset(g2,0,sizeof(g2));memset(g3,0,sizeof(g3));memset(g4,0,sizeof(g4));
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,40);w32(0,G(0x4a961e),OBJ+0x900);
    w32(0,ACT,VT);w32(0,VT+0x10,SLOT10);w32(0,VT+0x74,SLOT74);
    w32(0,PK,SEG);w32(0,PK+0xa,TABLE);w32(0,PK+0xe,0xabcdef01u);
    w32(0,SEG,POSE);w32(0,SEG+4,1000);w16(0,SEG+8,50);
    w32(0,ACT+0x1f2,CONF);w32(0,ACT+0x205,PARTS);
    for(unsigned i=0;i<12;++i)w16(0,TABLE+2*i,(uint16_t)(0x100+i));
    w32(0,REC,0x77000000u);w32(0,REC+6,0x66000000u);
    calls=0;answers=0;
}
static void set(unsigned i,uint32_t eax,uint32_t edx,uint32_t ecx){script[i].eax=eax;script[i].edx=edx;script[i].ecx=ecx;}
/* One logged call: function, arguments, entry EAX/EDX/ECX. */
static void expect(unsigned i,uint32_t f,unsigned n,const uint32_t *a,uint32_t eax,uint32_t edx,uint32_t ecx){
    CHECK(i<calls);const logged *l=log_+i;
    if(l->f!=f||l->n!=n||(n&&memcmp(l->a,a,4*n))||l->r[0]!=eax||l->r[1]!=edx||l->r[2]!=ecx){
        fprintf(stderr,"hit_test: call %u f %08x/%08x n %u/%u regs %08x %08x %08x / %08x %08x %08x\n",i,f,l->f,n,l->n,
            eax,edx,ecx,l->r[0],l->r[1],l->r[2]);
        for(unsigned k=0;k<n;++k)fprintf(stderr,"  arg%u %08x/%08x\n",k,a[k],l->a[k]);
        exit(1);
    }
    ++checks;
}
#define A(...) ((const uint32_t[]){__VA_ARGS__})
static void base_checks(void){
    es2_weapon_graph g=graph();
    /* Object query (q = 1), default damage word, no part damage (dead),
     * failed allocation, debris at the threshold 400h. */
    reset();w16(0,PK+4,0);w16(0,PK+6,0);w16(0,CONF+4,7);w16(0,PARTS+5,1);w8(ACT+0x99,3);w16(0,G(0x4a95f2),0x4321);
    out_dword=99;
    set(0,0xaaaa0001u,0xbbbb0000u,0xcccc0000u);         /* object_query */
    set(1,0x1u,0x2u,0x3u);                              /* transform_point */
    set(2,0,0x44u,0x45u);                               /* pool_alloc */
    set(3,0xffff7400u,0x5u,0x6u);                       /* rng_next: s16 & fffh = 400h */
    set(4,REC,0x7u,0x8u);                               /* 4089bc */
    set(5,0x9u,0xdead0001u,0xbu);                       /* 408530 */
    uint64_t v=es2_base_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)0xdead0001u<<32|100u)&&calls==7);
    expect(0,G(0x49630c),0,NULL,G(0x4974e6),entry[1],entry[2]);
    expect(1,G(0x427da8),4,A(F(0x3c),SEG,F(0x34),1),1,F(0x34),F(0x3c));
    CHECK(r32(0,F(0x34))==ACT&&r16(0,F(0x2a))==0x4321&&r32(0,F(0x30))==0xffffffffu);
    expect(2,G(0x48043c),3,A(F(0x80),POSE,F(0x74)),POSE,F(0x74),F(0x80));
    CHECK(r32(0,F(0x80))==0&&r32(0,F(0x7c))==100&&r32(0,F(0x78))==0);
    expect(3,G(0x471b30),1,A(OBJ+0x900),OBJ+0x900,0x2u,0x3u);
    expect(4,G(0x492ee0),1,A(G(0x4d2599)),0,0x44u,OBJ+0x900);
    expect(5,G(0x4089bc),1,A(F(0x46)),1,F(0x46),0x400u);
    expect(6,G(0x408530),4,A(0x77000000u+10u,0x66000000u,F(0x74),0),0x77000000u+10u,10u,1u);
    /* COL path, lazy matrix, part damage, explosion, no debris (401h). */
    reset();w16(0,PK+4,0x55);w16(0,PK+6,9);w16(0,CONF+4,0xffff);w32(0,CONF+0x38,OBJ+0x600);w32(0,ACT+0x201,OBJ+0x700);
    w32(0,HIT,0x40);w16(0,HIT+4,0xfffe);
    set(0,0xffff0010u,0x1u,0x2u);                       /* slot +10h: AX = 10h */
    set(1,1000u+50u+16u,0x3u,0x4u);                     /* dist_gate: d = reach */
    set(2,0x5u,0x6u,0x7u);                              /* euler_matrix */
    set(3,0x8u,0x9u,0xau);                              /* matrix_compose */
    set(4,HIT,0xbu,0xcu);                               /* col_query */
    set(5,0xdu,0xeu,0xfu);                              /* transform_point */
    set(6,0x10u,0x11u,0x12u);                           /* slot +74h */
    set(7,EXPL,0x13u,0x14u);                            /* pool_alloc */
    set(8,0x12345u,0x15u,0x16u);                        /* rng_next: k = 1 */
    set(9,0x17u,0x18u,0x19u);                           /* explosion_create */
    set(10,0x0401u,0xfeed0002u,0x1bu);                  /* rng_next: no debris */
    v=es2_base_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)0xfeed0002u<<32|0x41u)&&calls==12);
    expect(1,SLOT10,1,A(ACT),0,CONF,VT);
    expect(2,G(0x49288c),2,A(POSE+0x14,ACT+0x26),1050u,ACT+0x26,POSE+0x14);
    expect(3,G(0x47ebb8),2,A(ACT+0xc,ACT+0x12),ACT+0x12,ACT+0xc,SEG+0xa);
    CHECK(r16(0,ACT+0x32)==1);
    expect(4,G(0x47fa20),3,A(ACT+0x12,SEG+0xa,F(0x68)),ACT+0x12,F(0x68),SEG+0xa);
    expect(5,G(0x40c9d4),5,A(OBJ+0x600,ACT,SEG,F(0x68),OBJ+0x700),OBJ+0x600,F(0x68),CONF);
    CHECK(r32(0,F(0x28))==0x41&&r32(0,F(0x30))==0xfffffffeu);
    expect(6,G(0x48043c),3,A(F(0x80),POSE,F(0x74)),POSE,F(0x74),F(0x80));
    expect(7,SLOT74,4,A(ACT,(PK&0xffff0000u)|0xfffe,((ACT+0x92)&0xffff0000u)|0x55,0xabcdef01u),
        ((ACT+0x92)&0xffff0000u)|0x55,(PK&0xffff0000u)|0xfffe,VT);
    expect(8,G(0x471b30),1,A(OBJ+0x900),OBJ+0x900,0x11u,0x12u);
    expect(9,G(0x492ee0),1,A(G(0x4d2599)),EXPL,F(0x74),OBJ+0x900);
    CHECK(r16(0,F(0x14))==8&&r32(0,COUNTER)==36);
    expect(10,G(0x407f1c),5,A(EXPL,0x109u,F(0x74),ACT,1),EXPL,TABLE,0x109u);
    expect(11,G(0x492ee0),1,A(G(0x4d2599)),0x17u,COUNTER,0x19u);
    /* Distance miss: d = reach + 1; EDX = the saved head. */
    reset();w16(0,CONF+4,0xffff);w32(0,CONF+0x38,1);w16(0,ACT+0x32,1);
    set(0,0x10u,0,0);set(1,1000u+50u+16u+1u,0,0);
    v=es2_base_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32)&&calls==3);
    /* Object query hit of length -1: result 0, EDX = the query's. */
    reset();w16(0,PK+4,1);w16(0,CONF+4,7);w32(0,CONF+0x38,0);w16(0,PARTS+5,5);out_dword=0xffffffffu;
    set(0,0x10000u|1u,0xbeefu,0);
    v=es2_base_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)0xbeefu<<32)&&calls==2);
    expect(1,G(0x427da8),4,A(F(0x3c),SEG,F(0x34),1),0,F(0x34),F(0x3c));
}
static void mech_checks(void){
    es2_weapon_graph g=graph();
    /* Distance miss: EDX = s16(C+1Ah). */
    reset();w16(0,CONF+0x1a,0xfff0);
    set(0,1000u+50u-16u+1u,0,0);
    uint64_t v=es2_mech_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)0xfffffff0u<<32)&&calls==2);
    expect(0,G(0x49630c),0,NULL,G(0x49a138),entry[1],entry[2]);
    expect(1,G(0x49288c),2,A(POSE+0x14,ACT+0x26),ACT+0x26,POSE+0x14,entry[2]);
    /* Shield returns 0: EDX = the saved head; the AI reaction for kind 2. */
    reset();w16(0,PK+0x12,2);w16(0,PK+6,300);w16(0,ACT+0x32,1);out_word=300;
    set(0,10,0,0);set(1,0,0,0);set(2,0,0,0);
    v=es2_mech_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32)&&calls==4&&r8(ACT+0x96)==0&&r16(0,ACT+0x26b)==6000);
    expect(2,G(0x47fa20),3,A(ACT+0x12,SEG+0xa,F(0x64)),ACT+0x12,F(0x64),SEG+0xa);
    expect(3,G(0x413cc4),5,A(ACT+0x222,ACT,F(0x64),1000,F(0x32)),ACT+0x222,1000,F(0x64));
    /* Fully absorbed: explosion with type word k of the table. */
    reset();w16(0,PK+0x12,1);w16(0,PK+6,300);w32(0,ACT+0x288,5);w8(ACT+0x94,9);out_word=0;
    set(0,10,0,0);set(1,0x1u,0x2u,0x3u);set(2,0x4u,0x5u,0x6u);set(3,77,0x7u,0x8u);
    set(4,0x9u,0xau,0xbu);set(5,EXPL,0xcu,0xdu);set(6,0x3u,0xffff0000u,0xeu);set(7,0x10u,0x11u,0x12u);
    v=es2_mech_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32|77u)&&calls==9);
    CHECK(r8(ACT+0x96)==1&&r8(ACT+0x94)==0&&r32(0,ACT+0x288)==305&&r32(0,COUNTER)==36&&r16(0,F(0x14))==8);
    expect(2,G(0x47ebb8),2,A(ACT+0xc,ACT+0x12),ACT+0x12,ACT+0xc,ACT+0xc);
    expect(5,G(0x48043c),3,A(F(0x7c),POSE,F(0x70)),POSE,F(0x70),F(0x7c));
    CHECK(r32(0,F(0x78))==77);
    expect(6,G(0x471b30),1,A(OBJ+0x900),OBJ+0x900,0xau,0xbu);
    expect(7,G(0x492ee0),1,A(G(0x4d2599)),EXPL,F(0x70),OBJ+0x900);
    expect(8,G(0x407f1c),5,A(EXPL,0xffff0103u,F(0x70),ACT,1),TABLE,0xffff0103u,EXPL);
    /* Penetrating player hit: shield warning, COL hit, part hit. */
    reset();w8(ACT+0xa3,1);w16(0,PK+6,300);w16(0,PK+4,0xfff6);w16(0,ACT+0x32,1);w16(0,ACT+0x222,200);w16(0,ACT+0x224,299);
    w32(0,ACT+0x1f6,OBJ+0x600);w32(0,ACT+0x20e,OBJ+0x700);w32(0,HIT,0x123);w16(0,HIT+4,5);out_word=100;
    set(0,10,0,0);set(1,0x1u,0x2u,0x3u);set(2,55,0x7u,0x8u);set(3,0x9u,0xau,0xbu);
    set(4,HIT,0xcu,0xdu);set(5,0xeu,0xfu,0x10u);set(6,0x11u,0x12u,0x13u);
    v=es2_mech_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32|0x123u)&&calls==8&&r8(ACT+0xb0)==1&&r32(0,ACT+0x288)==200u-10u);
    expect(4,G(0x420a68),1,A(0x15),((ACT+0x222)&0xffff0000u)|200u,200u,499u);
    expect(5,G(0x40c9d4),5,A(OBJ+0x600,ACT,SEG,F(0x64),OBJ+0x700),OBJ+0x600,OBJ+0x700,F(0x64));
    expect(6,G(0x48043c),3,A(F(0x88),POSE,F(0x70)),POSE,F(0x70),F(0x88));
    CHECK(r32(0,F(0x84))==0x123);
    expect(7,G(0x4188c8),4,A(ACT,5,PK,F(0x70)),F(0x70),HIT,5);
    /* Sum 500: no warning; COL miss returns 0. */
    reset();w8(ACT+0xa3,1);w16(0,ACT+0x32,1);w16(0,ACT+0x222,200);w16(0,ACT+0x224,300);out_word=1;
    set(0,10,0,0);set(1,0,0,0);set(2,55,0,0);set(3,0,0x99u,0x98u);
    v=es2_mech_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32)&&calls==5&&r8(ACT+0xb0)==0);
}
static void flyer_checks(void){
    es2_weapon_graph g=graph();
    /* The view subject is exempt. */
    reset();w32(0,G(0x49ef34),1);w32(0,G(0x4d2684),ACT);
    uint64_t v=es2_flyer_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32)&&calls==1);
    expect(0,G(0x49630c),0,NULL,G(0x49a4cc),entry[1],entry[2]);
    /* A hit on a dead FLYER: type 10, debris word 3. */
    reset();w32(0,G(0x49ef34),1);w32(0,G(0x4d2684),ACT+1);w16(0,ACT+0x32,1);w16(0,PK+4,0x77);
    w32(0,ACT+0x1fa,CONF);w32(0,CONF+0x32,OBJ+0x600);w32(0,ACT+0x208,OBJ+0x700);w32(0,HIT,0x200);w16(0,HIT+4,4);w8(ACT+0x99,1);
    set(0,0x20u,0,0);set(1,1000u+50u+32u,0x1u,0x2u);set(2,0x3u,0x4u,0x5u);set(3,HIT,0x6u,0x7u);
    set(4,0x8002u,0x9u,0xau);set(5,0xbu,0xcu,0xdu);set(6,0xeu,0xfu,0x10u);set(7,EXPL,0x11u,0x12u);
    set(8,0x13u,0x14u,0xabcd0015u);set(9,REC,0x16u,0x17u);set(10,0x18u,0x19u,0x1au);
    v=es2_flyer_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32|0x201u)&&calls==12);
    expect(1,SLOT10,1,A(ACT),HEAD,PK,VT);
    expect(2,G(0x49288c),2,A(POSE+0x14,ACT+0x26),1050u,ACT+0x26,POSE+0x14);
    expect(3,G(0x47fa20),3,A(ACT+0x12,SEG+0xa,F(0x5c)),ACT+0x12,F(0x5c),SEG+0xa);
    expect(4,G(0x40c9d4),5,A(OBJ+0x600,ACT,SEG,F(0x5c),OBJ+0x700),OBJ+0x600,F(0x5c),CONF);
    expect(5,G(0x492ee0),1,A(G(0x4d2599)),HIT,0x6u,0x7u);
    expect(6,SLOT74,4,A(ACT,(PK&0xffff0000u)|4u,0x77u,0xabcdef01u),VT,0x77u,(PK&0xffff0000u)|4u);
    expect(7,G(0x48043c),3,A(F(0x74),POSE,F(0x68)),POSE,F(0x68),F(0x74));
    CHECK(r32(0,F(0x70))==0x201);
    expect(8,G(0x471b30),1,A(OBJ+0x900),OBJ+0x900,0xfu,0x10u);
    expect(9,G(0x407f1c),5,A(EXPL,((OBJ+0x900)&0xffff0000u)|10u,F(0x68),0,1),EXPL,F(0x68),((OBJ+0x900)&0xffff0000u)|10u);
    expect(10,G(0x4089bc),1,A(F(0x3a)),F(0x3a),COUNTER,0xabcd0003u);
    expect(11,G(0x408530),4,A(0x77000000u+30u,0x66000000u,F(0x68),0),0x77000000u+30u,3u,30u);
    /* A live FLYER: type word 10h+2k of the table, debris word 1. */
    reset();w16(0,ACT+0x32,0);w32(0,ACT+0x1fa,CONF);w32(0,CONF+0x32,OBJ+0x600);w32(0,HIT,7);
    set(0,0x20u,0,0);set(1,1000u+50u+32u,0,0);set(2,0,0,0);set(3,0,0,0);set(4,HIT,0,0);set(5,0x1u,0,0);
    set(6,0,0,0);set(7,0,0,0);set(8,0,0x31u,0x32u);set(9,REC,0,0);set(10,0,0x5150u,0);
    v=es2_flyer_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32|8u)&&calls==12&&r16(0,ACT+0x32)==1);
    expect(3,G(0x47ebb8),2,A(ACT+0xc,ACT+0x12),ACT+0x12,ACT+0xc,SEG+0xa);
    expect(10,G(0x4089bc),1,A(F(0x3a)),F(0x3a),0x31u,((OBJ+0x900)&0xffff0000u)|1u);
    expect(11,G(0x408530),4,A(0x77000000u+10u,0x66000000u,F(0x68),0),0x77000000u+10u,1u,10u);
    /* Distance miss and COL miss: 0 with the saved head. */
    reset();set(0,0x20u,0,0);set(1,1000u+50u+33u,0,0);
    v=es2_flyer_hit_test_registers(&memory,&g,ACT,PK,entry);
    CHECK(v==((uint64_t)HEAD<<32)&&calls==3);
}
int main(void){
    base_checks();mech_checks();flyer_checks();
    printf("hit_test: %u checks passed\n",checks);
    return 0;
}
