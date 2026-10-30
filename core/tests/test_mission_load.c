#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/mission_load.h"
/* Synthetic state for the core (mission load pieces L1 to L3). The
 * image base is placed so that the globals 4a9e00..4aa9d2 and the RNG
 * stream 4d2599 fall inside the arena. A logging call function stands in
 * for every game call (target, arguments, count, entry EAX/EDX/ECX); it
 * zeroes memory for memset 49681a, returns the block BLOCK for new[]
 * 4774f0, walks a scripted object list for pool_prev 471c70 and returns
 * EAX = A0h+n, EDX = D0h+n, ECX = C0h+n otherwise (n = call number), or the
 * scripted rng values. Expected values are worked out by hand from the
 * contract in mission_load.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x100u-0xa95a0u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJ (BASE+0x40000u)
#define BLOCK (BASE+0x50000u)
#define HEADER (BASE+0x60000u)
static uint8_t arena[0x70000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mission_load line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct call { uint32_t va,arg[5],count,eax,edx,ecx; } call;
static call calls[8];static unsigned call_count,observed,slot_seen;
static uint32_t objects[4],rng[2];static unsigned rng_next_value;
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<8);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;for(unsigned i=0;i<n&&i<5;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(k->va==0x49681au){for(uint32_t i=0;i<a[2];++i)((uint8_t *)host(0,a[0]+i))[0]=(uint8_t)a[1];
        r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    if(k->va==0x4774f0u){r->eax=BLOCK;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;return;}
    if(k->va==0x471c70u){unsigned i=0;if(a[1])while(i<4&&objects[i]!=a[1])++i,(void)0;
        uint32_t next=!a[1]?objects[0]:i+1<4?objects[i+1]:0;r->eax=next;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;return;}
    if(k->va==0x492ee0u){r->eax=r->edx=rng[rng_next_value++%2];return;}
    r->eax=0xa0u+call_count;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;if(slot)++slot_seen;++observed;}
static const es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(void){memset(arena,0xcc,sizeof(arena));call_count=observed=slot_seen=rng_next_value=0;}
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}

static void test_pool_create(void){
    uint32_t ecx=0;
    setup();
    /* Capacity 3, size 10h: stride 18h, total 48h. */
    uint64_t p=es2_pool_create_registers(&memory,&graph,HEADER,0x77770003u,0x88880010u,entry,&ecx);
    CHECK(call_count==2&&observed==2);
    CHECK(calls[0].va==0x4774f0u&&calls[0].count==1&&calls[0].arg[0]==0x48&&calls[0].eax==0x48&&calls[0].edx==0&&calls[0].ecx==0x48);
    CHECK(calls[1].va==0x49681au&&calls[1].count==3&&calls[1].arg[0]==BLOCK&&calls[1].arg[1]==0&&calls[1].arg[2]==0x48);
    CHECK(calls[1].eax==0x48&&calls[1].edx==0xd1u&&calls[1].ecx==0x48);
    CHECK(r16(0,HEADER)==3&&r16(0,HEADER+2)==0&&r16(0,HEADER+4)==0x10&&r32(0,HEADER+6)==BLOCK&&r32(0,HEADER+0xa)==BLOCK&&r32(0,HEADER+0xe)==0);
    CHECK(r32(0,BLOCK)==BLOCK+0x18&&r32(0,BLOCK+4)==0&&r32(0,BLOCK+0x18)==BLOCK+0x30&&r32(0,BLOCK+0x1c)==BLOCK);
    CHECK(r32(0,BLOCK+0x30)==0&&r32(0,BLOCK+0x34)==BLOCK+0x18&&r32(0,BLOCK+0x38)==0&&arena[BLOCK+0x48-BASE]==0xcc);
    CHECK(lo(p)==HEADER&&hi(p)==BLOCK+0x30&&ecx==0x48);
    /* Capacity 0: no new[], memset(0, 0, 0) with the IMUL's EDX 0 and the
     * entry ECX; EDX = memset's high word (0) | size. */
    setup();
    p=es2_pool_create_registers(&memory,&graph,HEADER,0x12340000u,0x5678002au,entry,&ecx);
    CHECK(call_count==1&&calls[0].va==0x49681au&&calls[0].arg[0]==0&&calls[0].arg[2]==0&&calls[0].eax==0&&calls[0].edx==0&&calls[0].ecx==0xe2);
    CHECK(r16(0,HEADER)==0&&r16(0,HEADER+4)==0x2a&&r32(0,HEADER+6)==0&&r32(0,HEADER+0xe)==0);
    CHECK(lo(p)==HEADER&&hi(p)==0x2a&&ecx==0);
    /* Size FFF8h (-8): stride 0, total 0; both nodes at the block. */
    setup();
    p=es2_pool_create_registers(&memory,&graph,HEADER,2,0xfff8u,entry,&ecx);
    CHECK(call_count==2&&calls[0].arg[0]==0&&r32(0,BLOCK)==0&&r32(0,BLOCK+4)==BLOCK&&hi(p)==BLOCK&&ecx==0);
    /* Capacity 1, size 8: one node, its own link cleared. */
    setup();
    p=es2_pool_create_registers(&memory,&graph,HEADER,1,8,entry,&ecx);
    CHECK(call_count==2&&calls[0].arg[0]==16&&r32(0,BLOCK)==0&&r32(0,BLOCK+4)==0&&hi(p)==BLOCK&&ecx==16);
}

static void test_pool_nth(void){
    uint32_t ecx=0;
    setup();
    objects[0]=OBJ;objects[1]=OBJ+0x100;objects[2]=OBJ+0x200;objects[3]=0;
    uint64_t p=es2_pool_nth_registers(&memory,&graph,0x1234,0xffff0001u,entry,&ecx);
    CHECK(call_count==2&&calls[0].arg[0]==0x1234&&calls[0].arg[1]==0&&calls[0].eax==0&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].arg[1]==OBJ&&calls[1].eax==OBJ&&calls[1].edx==((0xd1u&0xffff0000u)|1u)&&calls[1].ecx==0xc1);
    CHECK(lo(p)==OBJ+0x100&&hi(p)==0&&ecx==0xc2);
    /* Index 5 of three objects: 0 after the fourth call. */
    setup();
    p=es2_pool_nth_registers(&memory,&graph,0x1234,5,entry,&ecx);
    CHECK(call_count==4&&lo(p)==0&&hi(p)==0xd4&&ecx==0xc4);
    /* Index 0: the first object. */
    setup();
    p=es2_pool_nth_registers(&memory,&graph,0x1234,0,entry,&ecx);
    CHECK(call_count==1&&lo(p)==OBJ&&hi(p)==0);
}

static void test_subject_resolve(void){
    uint32_t ecx=0;
    setup();
    w32(0,VA(0x4a9e00),0x5000);
    uint64_t p=es2_subject_resolve_registers(&memory,&graph,0x7777fffeu,0,entry,&ecx);
    CHECK(call_count==0&&lo(p)==0x5000u-244u&&hi(p)==0xfffffffeu&&ecx==0xe2);
    /* Scope 1: remap byte FFh (-1) at index 3. */
    setup();
    ((uint8_t *)host(0,VA(0x4aa78e)+3))[0]=0xff;w32(0,VA(0x4a9b7a),0x6000);
    objects[0]=OBJ;objects[1]=0;objects[2]=0;objects[3]=0;
    p=es2_subject_resolve_registers(&memory,&graph,3,1,entry,&ecx);
    CHECK(call_count==1&&calls[0].va==0x471cb0u&&calls[0].arg[0]==0x6000&&calls[0].arg[1]==0xffffffffu);
    CHECK(calls[0].eax==0&&calls[0].edx==0xffffffffu&&calls[0].ecx==0x6000&&lo(p)==0xa1&&hi(p)==0xd1&&ecx==0xc1);
    /* Scope 2 and 3: the pool and the remapped position in other registers. */
    setup();
    ((uint8_t *)host(0,VA(0x4aa888)+1))[0]=2;w32(0,VA(0x4a9db9),0x7000);
    es2_subject_resolve_registers(&memory,&graph,1,2,entry,&ecx);
    CHECK(calls[0].arg[0]==0x7000&&calls[0].arg[1]==2&&calls[0].eax==2&&calls[0].edx==0x7000&&calls[0].ecx==0xe2);
    setup();
    ((uint8_t *)host(0,VA(0x4aa946)+4))[0]=0x7f;w32(0,VA(0x4a95a0),0x8000);
    es2_subject_resolve_registers(&memory,&graph,4,3,entry,&ecx);
    CHECK(calls[0].arg[0]==0x8000&&calls[0].arg[1]==0x7f&&calls[0].eax==0x8000&&calls[0].edx==0xe1&&calls[0].ecx==0x7f);
    setup();
    p=es2_subject_resolve_registers(&memory,&graph,4,9,entry,&ecx);
    CHECK(call_count==0&&lo(p)==0&&hi(p)==0xe1&&ecx==0xe2);
}

static void test_action_copy(void){
    uint32_t ecx=0;
    setup();
    for(unsigned i=0;i<10;++i){w16(0,OBJ+0x1000+2*i,(uint16_t)(0x100+i));w16(0,OBJ+0x1100+2*i,(uint16_t)(0x200+i));}
    uint64_t p=es2_action_copy_registers(&memory,OBJ,OBJ+0x1000,OBJ+0x1100,&ecx);
    for(unsigned i=0;i<10;++i)CHECK(r16(0,OBJ+0x1ba+2*i)==0x100+i&&r16(0,OBJ+0x1ce + 2*i)==0x200+i);
    CHECK(lo(p)==OBJ+0x1ce&&hi(p)==OBJ+0x1014&&ecx==OBJ+0x1114);
    /* The ids overlap the action words: each read sees the earlier writes. */
    setup();
    for(unsigned i=0;i<10;++i)w16(0,OBJ+0x1ba+2*i,(uint16_t)(0x300+i));
    w16(0,OBJ+0x1ce,0x4444);
    es2_action_copy_registers(&memory,OBJ,OBJ+0x1c4,OBJ+0x1ba,&ecx);
    /* Word 0: ids[0] = A+1C4h (305h) -> A+1BAh; actions[0] = A+1BAh (now 305h) -> A+1CEh. */
    CHECK(r16(0,OBJ+0x1ba)==0x305&&r16(0,OBJ+0x1ce)==0x305);
}

static void test_condition_init(void){
    uint32_t ecx=0;
    setup();
    const uint32_t C=OBJ+0x206,T=OBJ+0x2000,TB=OBJ+0x3000,L=OBJ+0x4000,O1=OBJ+0x5000,O2=OBJ+0x5100,W=OBJ+0x6000;
    w32(0,C,O2);w32(0,C+4,O1);w32(0,C+0xc,T);w32(0,T,TB);w32(0,T+4,L);
    for(unsigned r=0;r<29;++r)w16(0,TB+18*r,(uint16_t)(200+r));
    for(unsigned i=0;i<22;++i)w16(0,L+2*i,(uint16_t)(50+i));
    for(unsigned i=0;i<13;++i)w16(0,W+2*i,(uint16_t)(10*i));          /* first block */
    for(unsigned i=0;i<10;++i)w16(0,W+0x40+2*i,(uint16_t)(5*i));      /* second */
    for(unsigned i=0;i<10;++i)w16(0,W+0x80+2*i,i==3?0xffffu:(uint16_t)(i*20)); /* third */
    uint64_t p=es2_condition_init_registers(&memory,C,W,W+0x40,W+0x80,&ecx);
    /* Table 1, rows 0..12: (100 - 10i) x (200+i) / 100; rows 13..18: 0. */
    CHECK(r16(0,O1)==200&&r16(0,O1+2)==(uint16_t)(90*201/100)&&r16(0,O1+24)==(uint16_t)(-20*212/100)&&r16(0,O1+26)==0&&r16(0,O1+36)==0);
    /* Table 2, words 0..8: (100 - 5i) x (50+i) / 100; 9..11: 0. */
    CHECK(r16(0,O2)==50&&r16(0,O2+16)==(uint16_t)(60*58/100)&&r16(0,O2+18)==0&&r16(0,O2+22)==0);
    /* Third block i: sum = L[12+i] + TB[19+i] = 62+i + 219+i; q = (100 -
     * 20i) x sum / 100 against top = 219+i. i=0: q=281 > 219: table 1 word
     * 19 = FFFFh, q-top = 62 <= L[12] = 62: table 2 word 12 = 62. */
    CHECK(r16(0,O1+38)==0xffff&&r16(0,O2+24)==62);
    /* i=1: q = 80 x 283 / 100 = 226 > 220: 6 <= 63: word 13 = 6. */
    CHECK(r16(0,O1+40)==0xffff&&r16(0,O2+26)==6);
    /* i=2: q = 60 x 285 / 100 = 171 <= 221: table 1 word 21 = 171, table 2 untouched (CCh). */
    CHECK(r16(0,O1+42)==171&&r16(0,O2+28)==0xcccc);
    /* i=3: word FFFFh (-1): q = 101 x 287 / 100 = 289 > 222; 67 > L[15] = 65: FFFFh. */
    CHECK(r16(0,O1+44)==0xffff&&r16(0,O2+30)==0xffff);
    /* i=9: q = -80 x 299 / 100 = -239 (remainder -20): table 1 word 28. */
    CHECK(r16(0,O1+56)==(uint16_t)-239&&lo(p)==(uint32_t)-239&&hi(p)==(uint32_t)-20&&ecx==L+44);
}

static void test_region_timer_group(void){
    uint32_t ecx=0;
    setup();
    const uint32_t R=OBJ,REC=OBJ+0x100,P=0x40000;
    w16(0,REC,0);w16(0,REC+2,0xfffe);w16(0,REC+4,3);
    uint64_t p=es2_region_init_registers(&memory,R,REC,P,&ecx);
    CHECK(r16(0,R)==0&&r32(0,R+2)==P-24&&r32(0,R+6)==P+36&&lo(p)==R&&hi(p)==36&&ecx==((P-24)&0xffff0000u));
    w16(0,REC,7);w16(0,REC+4,0xfff0);
    p=es2_region_init_registers(&memory,R,REC,P,&ecx);
    CHECK(r16(0,R)==7&&r32(0,R+6)==(uint32_t)-160&&hi(p)==(uint32_t)-160&&ecx==(uint32_t)-16);
    /* Timer: first dword, timer_arm(T+2Ch, word) with EDX's high word, ten dwords. */
    setup();
    for(unsigned i=0;i<10;++i)w32(0,OBJ+0x200+4*i,0x1000+i);
    const uint32_t regs[3]={0xe0,0xabcd00e1u,0xe2};
    p=es2_timer_init_registers(&memory,&graph,OBJ,0x99,OBJ+0x200,0x5555fffeu,regs,&ecx);
    CHECK(call_count==1&&calls[0].va==0x467ac4u&&calls[0].arg[0]==OBJ+0x2c&&calls[0].arg[1]==0xabcdfffeu);
    CHECK(calls[0].eax==0x99&&calls[0].edx==0xabcdfffeu&&calls[0].ecx==OBJ+0x2c&&r32(0,OBJ)==0x99);
    for(unsigned i=0;i<10;++i)CHECK(r32(0,OBJ+4+4*i)==0x1000+i);
    CHECK(lo(p)==OBJ+0x228&&hi(p)==OBJ+0x2c&&ecx==10);
    /* Group array: 49667c(0, 7Ah, count, 1, 4240e4). */
    setup();
    p=es2_group_array_registers(&memory,&graph,OBJ,0x1234fffdu,entry,&ecx);
    CHECK(call_count==1&&calls[0].va==0x49667cu&&calls[0].count==5&&calls[0].arg[0]==0&&calls[0].arg[1]==0x7a);
    CHECK(calls[0].arg[2]==0xfffffffdu&&calls[0].arg[3]==1&&calls[0].arg[4]==VA(0x4240e4)&&calls[0].eax==0xfffffffdu);
    CHECK(r16(0,OBJ+4)==0xfffd&&r32(0,OBJ)==0xa1&&lo(p)==0xa1&&hi(p)==0xd1&&ecx==0xc1);
}

static void test_initial_damage(void){
    uint32_t ecx=0x5555u;
    const uint32_t A=OBJ,TYPE=OBJ+0x2000,VT=OBJ+0x3000,T=OBJ+0x4000,L=OBJ+0x5000,TAB=OBJ+0x6000;
    /* Word 80 (out of range): random_damage(A, 0, 25) with the entry EDX/ECX. */
    setup();
    uint64_t p=es2_initial_damage_registers(&memory,&graph,A,0x50,entry,&ecx);
    CHECK(call_count==1&&calls[0].va==0x417a04u&&calls[0].arg[1]==0&&calls[0].arg[2]==0x19&&calls[0].eax==0x19&&calls[0].edx==0xe1);
    CHECK(lo(p)==0xa1&&hi(p)==0xd1&&ecx==0x5555u);
    setup();
    es2_initial_damage_registers(&memory,&graph,A,0x1234003c,entry,&ecx);
    CHECK(calls[0].arg[1]==0x32&&calls[0].arg[2]==0x4b);
    setup();
    es2_initial_damage_registers(&memory,&graph,A,0x28,entry,&ecx);
    CHECK(calls[0].arg[1]==0x50&&calls[0].arg[2]==0x69);
    /* 20..39: multiply_q8(100h, word +0Ah of the list) into [A+206h]+0Ah. */
    setup();
    w32(0,A+0x206,TAB);w32(0,A+0x212,T);w32(0,T+4,L);w16(0,L+0xa,0xfff0);
    p=es2_initial_damage_registers(&memory,&graph,A,0x14,entry,&ecx);
    CHECK(call_count==2&&calls[0].va==0x47e0a0u&&calls[0].arg[0]==0x100&&calls[0].arg[1]==0xfffffff0u);
    CHECK(calls[0].eax==0xfffffff0u&&calls[0].edx==0xfffffff0u&&calls[0].ecx==L);
    CHECK(r16(0,TAB+0xa)==0xa1&&arena[A+0xb3-BASE]==1&&ecx==L);
    CHECK(calls[1].va==0x417a04u&&calls[1].arg[1]==0x78&&calls[1].arg[2]==0x91&&calls[1].eax==0x91&&calls[1].edx==0xd1&&calls[1].ecx==TAB);
    /* 0..19: rng bit 0 picks part 7/8 and 13/14 (type word +4Ah = 3). */
    setup();
    w32(0,A,VT);w32(0,VT+0x74,0x12345678u);w32(0,A+0x1f2,TYPE);w16(0,TYPE+0x4a,3);
    rng[0]=0x00010003u;rng[1]=0xfffe0002u;ecx=0x5555u;
    p=es2_initial_damage_registers(&memory,&graph,A,0,entry,&ecx);
    CHECK(call_count==5&&slot_seen==2);
    CHECK(calls[0].va==0x492ee0u&&calls[0].arg[0]==VA(0x4d2599)&&calls[0].eax==0&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x12345678u&&calls[1].count==4&&calls[1].arg[0]==A&&calls[1].arg[1]==0x00010007u&&calls[1].arg[2]==0x7d00&&calls[1].arg[3]==0);
    CHECK(calls[1].eax==VT&&calls[1].edx==0x00010003u&&calls[1].ecx==VA(0x4d2599));
    CHECK(calls[2].va==0x492ee0u&&calls[2].eax==0xa2&&calls[2].edx==TYPE&&calls[2].ecx==0xc2);
    CHECK(calls[3].arg[1]==0xfffe000eu&&calls[3].eax==0xfffe000eu&&calls[3].edx==VT&&calls[3].ecx==VA(0x4d2599));
    CHECK(calls[4].va==0x417a04u&&calls[4].arg[1]==0x96&&calls[4].arg[2]==0xaf&&calls[4].eax==0xaf&&calls[4].edx==A+0x92&&calls[4].ecx==0xc4);
    CHECK(arena[A+0xa4-BASE]==1&&arena[A+0xb3-BASE]==1&&arena[A+0xb4-BASE]==1&&lo(p)==0xa5&&ecx==0x5555u);
    /* Type word +4Ah = 2: one part only. */
    setup();
    w32(0,A,VT);w32(0,VT+0x74,0x12345678u);w32(0,A+0x1f2,TYPE);w16(0,TYPE+0x4a,2);
    es2_initial_damage_registers(&memory,&graph,A,0x13,entry,&ecx);
    CHECK(call_count==3&&calls[2].va==0x417a04u&&calls[2].edx==A+0x92&&calls[2].ecx==0xc2);
}

/* ---- L8: light_pools 4076e4, smoke_pools 4090cc, projectile_pools
 * 40a060. The construction counter pointer 6cbee4 lies outside the arena:
 * sp_memory maps its four bytes to sp_counter_slot. The fake 49630c writes
 * the saved head 5EADh at the record (f-2Ch for descriptor 497b16, else
 * f-28h) and clears its context word; new 47749c returns the scripted
 * sp_new values in order; other calls return A0h+n/D0h+n/C0h+n. sp_context
 * holds the record's context word at each later call. */
#define SP_SCRATCH (BASE+0x30000u)
#define SP_F (SP_SCRATCH+ES2_STAGE_POOLS_FRAME)
#define SP_TABLE (BASE+0x31000u)
#define SP_POOL (BASE+0x32000u)
#define SP_LIST (BASE+0x33000u)
#define SP_COUNTER (BASE+0x34000u)
static uint8_t sp_counter_slot[4];
static void *sp_host(void *c,uint32_t a){if(a-VA(0x6cbee4)<4u)return sp_counter_slot+(a-VA(0x6cbee4));return host(c,a);}
static uint16_t sp_r16(void *c,uint32_t a){uint8_t *p=sp_host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t sp_r32(void *c,uint32_t a){uint8_t *p=sp_host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void sp_w16(void *c,uint32_t a,uint16_t v){uint8_t *p=sp_host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void sp_w32(void *c,uint32_t a,uint32_t v){uint8_t *p=sp_host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory sp_memory={0,sp_r32,sp_r16,sp_w32,sp_w16,sp_host,0};
static uint32_t sp_new[3];static unsigned sp_new_count;static uint16_t sp_context[8];
static void sp_fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<8);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f-IMAGE+0x400000u;for(unsigned i=0;i<n&&i<5;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(k->va==0x49630cu){const uint32_t record=SP_F-(r->eax==VA(0x497b16)?0x2cu:0x28u);
        sp_w32(0,record,0x5eadu);w16(0,record+0x10,0);r->eax=0x5eadu;return;}
    sp_context[call_count-1]=sp_r16(0,SP_F-(calls[0].eax==VA(0x497b16)?0x1cu:0x18u));
    if(k->va==0x47749cu){CHECK(sp_new_count<3);r->eax=sp_new[sp_new_count++];r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;return;}
    r->eax=0xa0u+call_count;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;
}
static const es2_weapon_graph sp_graph={IMAGE,SP_SCRATCH,0,0,0,sp_fake_call,0,fake_observe,0};
static void sp_setup(uint32_t a,uint32_t b,uint32_t c){setup();sp_new[0]=a;sp_new[1]=b;sp_new[2]=c;sp_new_count=0;
    memset(sp_context,0,sizeof(sp_context));sp_w32(0,VA(0x6cbee4),SP_COUNTER);w32(0,SP_COUNTER,100);}

static void test_light_pools(void){
    uint32_t ecx=0;
    sp_setup(SP_TABLE,SP_POOL,SP_LIST);
    uint64_t p=es2_light_pools_registers(&sp_memory,&sp_graph,entry,&ecx);
    CHECK(call_count==7&&observed==7);
    CHECK(calls[0].va==0x49630cu&&calls[0].count==0&&calls[0].eax==VA(0x497b16)&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(calls[1].va==0x47749cu&&calls[1].arg[0]==0x404&&calls[1].eax==0x5eadu&&calls[1].edx==0xe1&&calls[1].ecx==0xe2&&sp_context[1]==0);
    CHECK(calls[2].va==0x406e44u&&calls[2].count==1&&calls[2].arg[0]==SP_TABLE&&calls[2].eax==SP_TABLE&&calls[2].edx==SP_TABLE&&calls[2].ecx==0x404&&sp_context[2]==0x14);
    CHECK(r32(0,SP_COUNTER)==95&&r32(0,VA(0x4a9608))==SP_TABLE);
    CHECK(r32(0,SP_TABLE+0x10)==0&&r32(0,SP_TABLE+0x14)==62&&r32(0,SP_TABLE+0xc)==10000);
    /* new(12h) with 406ee4's registers (table, 0, 62) */
    CHECK(calls[3].va==0x47749cu&&calls[3].arg[0]==0x12&&calls[3].eax==SP_TABLE&&calls[3].edx==0&&calls[3].ecx==62&&sp_context[3]==8);
    CHECK(calls[4].va==0x471ad8u&&calls[4].count==3&&calls[4].arg[0]==SP_POOL&&calls[4].arg[1]==3&&calls[4].arg[2]==0xe);
    CHECK(calls[4].eax==SP_POOL&&calls[4].edx==SP_POOL&&calls[4].ecx==0x12&&sp_context[4]==0x2c&&r32(0,VA(0x4a95fe))==SP_POOL);
    CHECK(calls[5].va==0x47749cu&&calls[5].arg[0]==8&&calls[5].eax==0xa5&&calls[5].edx==0xd5&&calls[5].ecx==SP_POOL&&sp_context[5]==0x20);
    CHECK(calls[6].va==0x4774f0u&&calls[6].arg[0]==100&&calls[6].eax==((SP_LIST&0xffff0000u)|0x19)&&calls[6].edx==100&&calls[6].ecx==8&&sp_context[6]==0x38);
    CHECK(r32(0,SP_LIST)==0xa7&&r16(0,SP_LIST+4)==0&&r16(0,SP_LIST+6)==0x19&&r32(0,VA(0x4a9602))==SP_LIST);
    CHECK(r16(0,SP_F-0x1c)==0x20&&lo(p)==0x5eadu&&hi(p)==SP_LIST&&ecx==SP_LIST);
    /* Pool and list allocations fail: both globals 0, no pool_create or
     * new[], ECX = 8 (POP ECX), EDX = 0. */
    sp_setup(SP_TABLE,0,0);
    p=es2_light_pools_registers(&sp_memory,&sp_graph,entry,&ecx);
    CHECK(call_count==5&&calls[4].va==0x47749cu&&calls[4].eax==0&&calls[4].edx==0xd4&&calls[4].ecx==0);
    CHECK(r32(0,VA(0x4a95fe))==0&&r32(0,VA(0x4a9602))==0&&lo(p)==0x5eadu&&hi(p)==0&&ecx==8&&r16(0,SP_F-0x1c)==8);
}
static void test_smoke_projectile_pools(void){
    uint32_t ecx=0;
    sp_setup(SP_POOL,SP_LIST,0);
    uint64_t p=es2_smoke_pools_registers(&sp_memory,&sp_graph,entry,&ecx);
    CHECK(call_count==6);
    CHECK(calls[0].va==0x49630cu&&calls[0].eax==VA(0x497f5e));
    CHECK(calls[1].va==0x47749cu&&calls[1].arg[0]==0x12&&calls[1].eax==0x5eadu&&calls[1].edx==0xe1&&calls[1].ecx==0xe2);
    CHECK(calls[2].va==0x471ad8u&&calls[2].arg[0]==SP_POOL&&calls[2].arg[1]==0x14&&calls[2].arg[2]==0x2a&&calls[2].ecx==0x12&&sp_context[2]==0x14);
    CHECK(calls[3].va==0x47749cu&&calls[3].arg[0]==8&&calls[3].ecx==SP_POOL&&calls[3].edx==0xd3&&sp_context[3]==8);
    CHECK(calls[4].va==0x4774f0u&&calls[4].arg[0]==100&&calls[4].ecx==8&&sp_context[4]==0x20);
    CHECK(calls[5].va==0x408fb8u&&calls[5].count==0&&calls[5].eax==SP_LIST&&calls[5].edx==SP_LIST&&calls[5].ecx==SP_LIST&&sp_context[5]==8);
    CHECK(r32(0,VA(0x4a9684))==SP_POOL&&r32(0,VA(0x4a9688))==SP_LIST&&r32(0,SP_LIST)==0xa5&&r16(0,SP_LIST+6)==0x19);
    CHECK(lo(p)==0x5eadu&&hi(p)==0xd6&&ecx==0xc6);
    /* Both allocations fail: 408fb8 gets (0, 0, 8). */
    sp_setup(0,0,0);
    p=es2_smoke_pools_registers(&sp_memory,&sp_graph,entry,&ecx);
    CHECK(call_count==4&&calls[3].va==0x408fb8u&&calls[3].eax==0&&calls[3].edx==0&&calls[3].ecx==8&&sp_context[3]==0);
    CHECK(r32(0,VA(0x4a9684))==0&&r32(0,VA(0x4a9688))==0&&lo(p)==0x5eadu);
    /* Projectiles: 160 x 66h, list of 80 (new[] 320). */
    sp_setup(SP_POOL,SP_LIST,0);
    p=es2_projectile_pools_registers(&sp_memory,&sp_graph,entry,&ecx);
    CHECK(call_count==5&&calls[0].eax==VA(0x4982ec));
    CHECK(calls[2].va==0x471ad8u&&calls[2].arg[1]==0xa0&&calls[2].arg[2]==0x66&&sp_context[2]==0x14);
    CHECK(calls[4].va==0x4774f0u&&calls[4].arg[0]==320&&calls[4].eax==((SP_LIST&0xffff0000u)|0x50)&&calls[4].edx==320);
    CHECK(r32(0,VA(0x4a96c2))==SP_POOL&&r32(0,VA(0x4a96c6))==SP_LIST&&r16(0,SP_LIST+6)==0x50&&r16(0,SP_LIST+4)==0&&r32(0,SP_LIST)==0xa5);
    CHECK(r16(0,SP_F-0x18)==8&&lo(p)==0x5eadu&&hi(p)==SP_LIST&&ecx==SP_LIST);
    /* Only the list fails: EDX = 0, ECX = 8. */
    sp_setup(SP_POOL,0,0);
    p=es2_projectile_pools_registers(&sp_memory,&sp_graph,entry,&ecx);
    CHECK(call_count==4&&r32(0,VA(0x4a96c2))==SP_POOL&&r32(0,VA(0x4a96c6))==0&&hi(p)==0&&ecx==8);
}

/* ---- L4: weapon_fit 4175dc, player_weapons 4104ec, weapon_factory
 * 40fff8. The image lies at WM_IMAGE (a 3 MB window, so that the globals
 * 6cbee4 and 6c5830 and the dispatch byte table at 4100fd are guest
 * memory); objects are in the arena. wm_call logs every game call (target,
 * up to seven slots, entry EAX/EDX/ECX, the context word of the record
 * when one is linked) and answers EAX = A0h+n, EDX = D0h+n, ECX = C0h+n
 * (n = call number from 1) unless a scripted answer below applies; the fake
 * 49630c writes the saved head 5EADh at the record (f-28h for descriptor
 * 49a0c0, f-44h for 499055) and clears its context word. The dispatch
 * bytes are the classes of weapon_classes.md (IDs 1-5, 13-16, 21: 8;
 * 6, 22: 7; 7-12, 17, 19, 20, 23-25, 28: 5; 26: 6; 18: 9; 29: 4; 30: 3;
 * 31: 2; 32: 1; 0, 27: 0). Expected values are worked out by hand from the
 * contract in mission_load.md. */
#define WM_IMAGE UINT32_C(0xa0000000)
#define WVA(x) (WM_IMAGE+(uint32_t)(x)-0x400000u)
#define WSCR (BASE+0x68000u)
static uint8_t wm_image[0x300000];
static void *wm_host(void *c,uint32_t a){if(a-WM_IMAGE<sizeof(wm_image))return wm_image+(a-WM_IMAGE);return host(c,a);}
static uint16_t wm_r16(void *c,uint32_t a){uint8_t *p=wm_host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t wm_r32(void *c,uint32_t a){uint8_t *p=wm_host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wm_w16(void *c,uint32_t a,uint16_t v){uint8_t *p=wm_host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wm_w32(void *c,uint32_t a,uint32_t v){uint8_t *p=wm_host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory wm_memory={0,wm_r32,wm_r16,wm_w32,wm_w16,wm_host,0};
typedef struct wm_rec { uint32_t va,arg[7],count,eax,edx,ecx,context; } wm_rec;
static wm_rec wm[24];static unsigned wm_count,wm_frame_record;
/* Scripted answers: by target VA, the EAX to return (and side effects). */
static uint32_t wm_new,wm_new2,wm_record7,wm_record6,wm_record18,wm_record37,wm_system,wm_slots,wm_vret;
static uint16_t wm_system_count;
static void wm_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(wm_count<24);wm_rec *k=wm+wm_count++;memset(k,0,sizeof(*k));
    k->va=f-WM_IMAGE<sizeof(wm_image)?f-WM_IMAGE+0x400000u:f;for(unsigned i=0;i<n&&i<7;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(wm_frame_record)k->context=wm_r16(0,wm_frame_record+0x10u);
    if(k->va==0x49630cu){const uint32_t f0=WSCR+(r->eax==WVA(0x49a0c0)?ES2_WEAPON_FIT_FRAME:ES2_WEAPON_FACTORY_FRAME);
        wm_frame_record=f0-(r->eax==WVA(0x49a0c0)?0x28u:0x44u);wm_w32(0,wm_frame_record,0x5eadu);wm_w16(0,wm_frame_record+0x10u,0);
        r->eax=0x5eadu;return;}
    r->eax=0xa0u+wm_count;r->edx=0xd0u+wm_count;r->ecx=0xc0u+wm_count;
    switch(k->va){
    case 0x420634u:r->eax=wm_system;break;   /* the mount table pointer (see the tests) */
    case 0x47749cu:r->eax=wm_new;break;
    case 0x4774f0u:r->eax=wm_new;wm_new+=0x100u;break;
    case 0x40fff8u:case 0x4104ecu:wm_w32(0,a[0]+4u,wm_slots);wm_w16(0,a[0]+8u,wm_system_count);break;
    case 0x40fe84u:r->eax=(uint16_t)a[0]==7?wm_record7:(uint16_t)a[0]==6?wm_record6:(uint16_t)a[0]==18?wm_record18:wm_record37;break;
    case 0x40ffc8u:r->eax=0x1e1e1e1eu;break;
    case 0x47a2c8u:r->eax=wm_new2;wm_new2=wm_new2?wm_new2+0x100u:0;break;
    case 0x40cb84u:r->eax=2;break;
    case 0x4d0000u:r->eax=wm_vret;break;      /* the shape virtual (arena vtable entry) */
    }
}
static const es2_weapon_graph wm_graph={WM_IMAGE,WSCR,0,0,0,wm_call,0,fake_observe,0};
static void wm_setup(void){
    setup();wm_count=0;wm_frame_record=0;memset(wm_image,0,sizeof(wm_image));
    static const uint8_t classes[33]={0,8,8,8,8,8,7,5,5,5,5,5,5,8,8,8,8,5,9,5,5,8,7,5,5,5,6,0,5,4,3,2,1};
    memcpy(wm_image+0x100fd,classes,33);
    wm_w32(0,WVA(0x6cbee4),BASE+0x3f000u);w32(0,BASE+0x3f000u,100);wm_w32(0,WVA(0x6c5830),0x4ea9u);
}

static void test_player_weapons(void){
    const uint32_t P=BASE+0x40010u,A=BASE+0x41000u,SL=BASE+0x42000u,W0=BASE+0x43000u,W2=BASE+0x43100u;
    const uint32_t C0=BASE+0x44000u,C2=BASE+0x44100u,G=BASE+0x45000u;
    uint32_t ecx=0;
    wm_setup();wm_new=G;wm_slots=SL;wm_system_count=9;
    w32(0,SL,W0);w32(0,SL+4,0);w32(0,SL+8,W2);w32(0,W0+0x1c,C0);w16(0,C0+0x56,0x12);w32(0,W2+0x1c,C2);w16(0,C2+0x56,5);
    uint64_t p=es2_player_weapons_registers(&wm_memory,&wm_graph,P,A,0x77770003u,0x1111u,0x2222u,0x55550044u,entry,&ecx);
    CHECK(wm_count==6&&observed==6);
    CHECK(wm[0].va==0x40fff8u&&wm[0].count==7&&wm[0].arg[0]==P&&wm[0].arg[1]==A&&wm[0].arg[2]==0x55550003u&&wm[0].arg[3]==0x1111u);
    CHECK(wm[0].arg[4]==0x2222u&&wm[0].arg[5]==0x55550044u&&wm[0].arg[6]==1&&wm[0].eax==0x55550003u&&wm[0].edx==A&&wm[0].ecx==P);
    CHECK(r32(0,P)==WVA(0x499238));
    CHECK(wm[1].va==0x49667cu&&wm[1].count==5&&wm[1].arg[0]==P+0x1f&&wm[1].arg[1]==6&&wm[1].arg[2]==3&&wm[1].arg[3]==1&&wm[1].arg[4]==WVA(0x411892));
    CHECK(wm[1].eax==P&&wm[1].edx==P+0x1f&&wm[1].ecx==0xc1);
    /* new[] of 2 x 3 bytes for each group; ECX = 6 after the first (POP ECX) */
    CHECK(wm[2].va==0x4774f0u&&wm[2].arg[0]==6&&wm[2].eax==6&&wm[2].edx==0xd2&&wm[2].ecx==0xc2);
    CHECK(wm[3].va==0x4774f0u&&wm[3].eax==6&&wm[3].edx==0xd3&&wm[3].ecx==6&&wm[4].edx==0xd4&&wm[4].ecx==6);
    CHECK(r32(0,P+0x1f)==G&&r16(0,P+0x23)==3&&r32(0,P+0x25)==G+0x100&&r16(0,P+0x29)==3&&r32(0,P+0x2b)==G+0x200&&r16(0,P+0x2f)==3);
    /* weapon 0 (ID 12h) in no group, weapon 2 (ID 5) in group 0 and selected */
    CHECK(r16(0,G)==0&&r16(0,G+0x100)==0&&r16(0,G+0x200)==0);
    CHECK(r16(0,G+4)==1&&r16(0,G+0x104)==0&&r16(0,G+0x204)==0&&r16(0,G+2)==0xcccc);
    CHECK(arena[P+0x1d-BASE]==2&&arena[P+0x16-BASE]==1&&r32(0,BASE+0x3f000u)==101);
    /* fire_group(P, 0) with EAX = the count, ECX = P's high word (CX 1, DEC) */
    CHECK(wm[5].va==0x410ae4u&&wm[5].arg[0]==P&&wm[5].arg[1]==0&&wm[5].eax==3&&wm[5].edx==P&&wm[5].ecx==(P&0xffff0000u));
    CHECK(lo(p)==P&&hi(p)==BASE+0x3f000u&&ecx==2);
    /* No weapons (count 0): ECX keeps the entry ECX (PUSH ECX), the byte stays FFh. */
    wm_setup();wm_new=G;wm_slots=SL;
    p=es2_player_weapons_registers(&wm_memory,&wm_graph,P,A,0,0,0,0,entry,&ecx);
    CHECK(wm_count==6&&wm[5].eax==0&&arena[P+0x1d-BASE]==0xff&&ecx==0xe2&&lo(p)==P);
}
static void test_weapon_factory(void){
    const uint32_t W=BASE+0x40000u,A=BASE+0x41000u,M=BASE+0x42000u,I=BASE+0x42100u,S2=BASE+0x42200u;
    const uint32_t R7=BASE+0x43000u,R6=BASE+0x43100u,SB=BASE+0x44000u,OA=BASE+0x45000u,COUNTER=BASE+0x3f000u;
    const uint32_t f=WSCR+ES2_WEAPON_FACTORY_FRAME;
    uint32_t ecx=0;
    wm_setup();wm_new=SB;wm_new2=OA;wm_record7=R7;wm_record6=R6;
    arena[M+0x17-BASE]=0;arena[M+0x1a+0x17-BASE]=1;arena[M+0x34+0x17-BASE]=0xff;
    w16(0,I,7);w16(0,I+2,0xffff);w16(0,I-2,6);w16(0,S2,5);w16(0,S2+2,9);w16(0,S2-2,0x33);
    w16(0,R7+0x3e,0x22);w16(0,R6+0x3e,0x21);w32(0,f-0x58,0x5e5e5e5eu);
    uint64_t p=es2_weapon_factory_registers(&wm_memory,&wm_graph,W,A,0x00010003u,M,I,S2,1,0xabcd0000u,entry,&ecx);
    CHECK(wm_count==11&&observed==11);
    CHECK(wm[0].va==0x49630cu&&wm[0].eax==WVA(0x499055)&&wm[0].edx==0xe1&&wm[0].ecx==0xe2);
    CHECK(r32(0,W)==WVA(0x499338)&&r32(0,W+4)==SB&&r16(0,W+8)==3);
    CHECK(wm[1].va==0x4774f0u&&wm[1].arg[0]==12&&wm[1].eax==12&&wm[1].edx==0&&wm[1].ecx==COUNTER);
    CHECK(wm[2].va==0x474bbcu&&wm[2].arg[0]==0x30d40u&&wm[2].eax==W+4&&wm[2].edx==SB&&wm[2].ecx==3);
    /* mount 0: ID 7 (the caller's EBX high word), selector 22h, secondary 5 -> 0 */
    CHECK(wm[3].va==0x40fe84u&&wm[3].arg[0]==0xabcd0007u&&wm[3].eax==5&&wm[3].edx==S2&&wm[3].ecx==0x30d40u);
    CHECK(wm[4].va==0x40ffc8u&&wm[4].count==2&&wm[4].arg[0]==0&&wm[4].arg[1]==0&&wm[4].eax==((R7&0xffff0000u)|0x22)&&wm[4].edx==0&&wm[4].ecx==0xabcd0007u);
    CHECK(wm[5].va==0x47a2c8u&&wm[5].arg[0]==0x83&&wm[5].arg[1]==0x4ea9u&&wm[5].eax==5&&wm[5].edx==0x4ea9u&&wm[5].ecx==0x4ea9u&&wm[5].context==0);
    CHECK(wm[6].va==0x40e074u&&wm[6].count==6&&wm[6].arg[0]==OA&&wm[6].arg[1]==A&&wm[6].arg[2]==M&&wm[6].arg[3]==R7&&wm[6].arg[4]==0x1e1e1e1eu);
    CHECK(wm[6].arg[5]==1&&wm[6].eax==A&&wm[6].edx==OA&&wm[6].ecx==0x1e1e1e1eu&&wm[6].context==0x14);
    /* mount 1: a negative item, slot 0; mount 2 (index byte -1): ID 6 (ELF),
     * selector 21h keeps the projectile record of mount 0 */
    CHECK(wm[7].va==0x40fe84u&&wm[7].arg[0]==6&&wm[7].eax==0xffff0033u&&wm[7].edx==S2&&wm[7].ecx==COUNTER&&r16(0,f-0x52)==0x33);
    CHECK(wm[8].va==0x47a2c8u&&wm[8].arg[0]==0x86&&wm[8].eax==7&&wm[8].edx==0xd8&&wm[8].ecx==0x4ea9u&&wm[8].context==8);
    CHECK(wm[9].va==0x40e074u&&wm[9].arg[0]==OA+0x100&&wm[9].arg[1]==A&&wm[9].arg[2]==M+0x34&&wm[9].arg[3]==R6&&wm[9].arg[4]==0x1e1e1e1eu);
    CHECK(wm[9].arg[5]==1&&wm[9].eax==A&&wm[9].edx==OA+0x100&&wm[9].ecx==M+0x34&&wm[9].context==0x2c);
    CHECK(r32(0,f-0x60)==0x1e1e1e1eu&&r32(0,f-0x64)==R6&&r32(0,f-0x68)==M+0x34&&r32(0,f-0x6c)==A&&r16(0,f-0x34)==0x20);
    CHECK(r32(0,SB)==OA&&r32(0,SB+4)==0&&r32(0,SB+8)==OA+0x100&&r32(0,OA+0x100)==WVA(0x4992c0));
    CHECK(wm[10].va==0x474be4u&&wm[10].count==0&&wm[10].eax==0&&wm[10].edx==(COUNTER&0xffff0000u)&&wm[10].ecx==OA+0x100);
    /* counter: 100 +1 -6 +1 -7 +1 */
    CHECK(r32(0,COUNTER)==90&&lo(p)==W&&hi(p)==COUNTER&&ecx==0x5eadu&&r16(0,f-0x48)==0xffff&&r32(0,f-0x50)==0);
    /* ECM (ID 18, selector 21h: the residue is not passed) with a failed
     * allocation, then ID 25h (above 20h: the slot is not written). */
    wm_setup();wm_new=SB;wm_new2=0;wm_record18=R7;wm_record37=R6;
    arena[M+0x17-BASE]=0;arena[M+0x1a+0x17-BASE]=1;w16(0,I,18);w16(0,I+2,0x25);w16(0,S2,0);w16(0,S2+2,0);
    w16(0,R7+0x3e,0x21);w16(0,R6+0x3e,3);w32(0,f-0x58,0x5e5e5e5eu);
    p=es2_weapon_factory_registers(&wm_memory,&wm_graph,W,A,2,M,I,S2,0,0,entry,&ecx);
    CHECK(wm_count==8&&wm[3].va==0x40fe84u&&wm[3].arg[0]==18&&wm[4].va==0x47a2c8u&&wm[4].arg[0]==0x81&&wm[4].eax==9);
    CHECK(wm[5].va==0x40fe84u&&wm[5].arg[0]==0x25&&wm[6].va==0x40ffb0u&&wm[6].arg[0]==((R6&0xffff0000u)|3)&&wm[6].eax==((R6&0xffff0000u)|3));
    CHECK(wm[7].va==0x474be4u&&wm[7].ecx==0x4ea9u&&r32(0,SB)==0&&r32(0,SB+4)==0xccccccccu&&r32(0,f-0x58)==0xa7u&&r32(0,f-0x14)==0);
    CHECK(r32(0,COUNTER)==102&&ecx==0x5eadu&&lo(p)==W);
}
static void test_weapon_fit(void){
    const uint32_t A=BASE+0x41000u,MT=BASE+0x47000u,SYS=BASE+0x48000u,SL=BASE+0x48100u,W0=BASE+0x49000u,D=BASE+0x4a000u;
    const uint32_t C=BASE+0x4b000u,Q=BASE+0x4c000u,X=BASE+0x4d000u,Y=BASE+0x4d100u,VT=BASE+0x4d200u,VR=BASE+0x4d300u;
    const uint32_t TP=BASE+0x4e000u,TBL=BASE+0x4e100u,CW=BASE+0x4f000u,COUNTER=BASE+0x3f000u;
    const uint32_t f=WSCR+ES2_WEAPON_FIT_FRAME,R=0xf0000000u;
    uint32_t ecx=0;
    wm_setup();wm_system=MT;wm_new=SYS;wm_slots=SL;wm_system_count=2;wm_vret=VR;
    arena[A+0xa3-BASE]=0;w16(0,MT,2);w32(0,MT+2,0x55667788u);
    w32(0,SL,W0);w32(0,SL+4,0);w32(0,W0+0x10,1);w32(0,W0+0x18,D);w32(0,W0+0x1c,C);arena[D+0x17-BASE]=2;
    w16(0,C+0x2a,0x0bad);w32(0,C+8,Q);
    w32(0,A+0x34,X);w32(0,X+4,Y);w32(0,Y,VT);w32(0,VT+0x20,0x4d0000u);w16(0,VR+4,0x0707);
    w32(0,A+0x1fa,TP);w32(0,TP,TBL);arena[TBL+18*21+4-BASE]=0x5a;
    for(unsigned k=0;k<29;++k)w16(0,TBL+18*k+6,(k==3||k==28)?0:1);
    w32(0,A+0x20e,CW);w32(0,A+0x1f6,0x12340000u);
    uint64_t p=es2_weapon_fit_registers(&wm_memory,&wm_graph,A,0x2222u,0x3333u,entry,&ecx);
    (void)R;
    CHECK(wm_count==18&&observed==18&&slot_seen==1);
    CHECK(wm[0].va==0x49630cu&&wm[0].eax==WVA(0x49a0c0)&&wm[0].edx==0xe1&&wm[0].ecx==0xe2);
    CHECK(wm[1].va==0x420634u&&wm[1].arg[0]==A&&wm[1].eax==0x5eadu&&wm[1].edx==0xe1&&wm[1].ecx==0xe2);
    CHECK(wm[2].va==0x47749cu&&wm[2].arg[0]==0x14&&wm[2].eax==A+0x92&&wm[2].edx==0xd2&&wm[2].ecx==A);
    CHECK(wm[3].va==0x40fff8u&&wm[3].count==7&&wm[3].arg[0]==SYS&&wm[3].arg[1]==A&&wm[3].arg[2]==((SYS&0xffff0000u)|2)&&wm[3].arg[3]==0x55667788u);
    CHECK(wm[3].arg[4]==0x2222u&&wm[3].arg[5]==0x3333u&&wm[3].arg[6]==0&&wm[3].eax==((SYS&0xffff0000u)|2)&&wm[3].edx==SYS&&wm[3].ecx==0x55667788u);
    CHECK(wm[3].context==0x2c&&r32(0,A+0x202)==SYS&&r32(0,COUNTER)==98);
    CHECK(wm[4].va==0x402604u&&wm[4].arg[0]==A&&wm[4].arg[1]==0&&wm[4].eax==SYS&&wm[4].edx==0xd4&&wm[4].ecx==COUNTER&&wm[4].context==0x20);
    CHECK(wm[5].va==0x40f540u&&wm[5].arg[0]==W0&&wm[5].eax==C&&wm[5].edx==C+0x12&&wm[5].ecx==C);
    CHECK(wm[6].va==0x40e61cu&&wm[6].arg[0]==W0&&wm[6].eax==0xe&&wm[6].edx==C&&wm[6].ecx==((D&0xffff0000u)|0x0bad));
    CHECK(wm[7].va==0x4d0000u&&wm[7].count==2&&wm[7].arg[0]==Y&&wm[7].arg[1]==0xa7&&wm[7].eax==VT&&wm[7].edx==X&&wm[7].ecx==Y);
    CHECK(wm[8].va==0x417530u&&wm[8].arg[0]==1&&wm[8].arg[1]==((VR&0xffff0000u)|0x0707)&&wm[8].eax==((VR&0xffff0000u)|0x0707)&&wm[8].edx==1&&wm[8].ecx==0xc8);
    /* the record words: C+18h = 2+13h, [C+8]+2 = 2+0Ch, C+4 = row 21's byte +4 */
    CHECK(r16(0,C+0x18)==0x15&&r16(0,Q+2)==0xe&&arena[C+4-BASE]==0x5a);
    CHECK(r32(0,f-0x2c)==C&&r32(0,f-0x30)==C+0x12&&r32(0,f-0x34)==C&&r32(0,f-0x38)==0xa6&&r16(0,f-0x3a)==0x15&&r16(0,f-0x3c)==0xe);
    CHECK(r16(0,f-0x3e)==0x0bad&&r16(0,f-0x40)==0xa7);
    CHECK(wm[9].va==0x40cec0u&&wm[9].arg[0]==0x12340000u&&wm[9].arg[1]==f-0x30&&wm[9].arg[2]==f-0x38&&wm[9].arg[3]==1);
    CHECK(wm[9].eax==f-0x38&&wm[9].edx==f-0x30&&wm[9].ecx==0x12340000u);
    CHECK(wm[10].va==0x40d240u&&wm[10].arg[0]==TP&&wm[10].arg[1]==f-0x3a&&wm[10].arg[2]==f-0x34&&wm[10].eax==f-0x34&&wm[10].edx==f-0x3a&&wm[10].ecx==TP);
    CHECK(wm[11].va==0x40d128u&&wm[11].arg[0]==TP&&wm[11].arg[1]==f-0x3c&&wm[11].arg[2]==f-0x3e&&wm[11].eax==f-0x3e&&wm[11].edx==f-0x3c&&wm[11].ecx==TP);
    CHECK(wm[12].va==0x40cb84u&&wm[12].arg[0]==0x12340000u&&wm[12].arg[1]==f-0x12c&&wm[12].arg[2]==f-0xf0&&wm[12].arg[3]==f-0xb4);
    CHECK(wm[12].eax==f-0xf0&&wm[12].edx==f-0x12c&&wm[12].ecx==0x12340000u);
    CHECK(wm[13].va==0x40d284u&&wm[13].count==5&&wm[13].arg[0]==TP&&wm[13].arg[1]==2&&wm[13].arg[2]==f-0x12c&&wm[13].arg[4]==f-0xb4);
    CHECK(wm[13].eax==TP&&wm[13].edx==f-0x12c&&wm[13].ecx==f-0xf0);
    /* condition words 3 and 28 cleared; ECX = [A+20Eh] from the last row */
    CHECK(r16(0,CW+6)==0&&r16(0,CW+56)==0&&r16(0,CW+4)==0xcccc&&r16(0,CW+8)==0xcccc);
    CHECK(wm[14].va==0x40fb2cu&&wm[14].arg[0]==SYS&&wm[14].arg[1]==A+0x307&&wm[14].eax==A+0x307&&wm[14].edx==SYS&&wm[14].ecx==CW);
    CHECK(wm[15].va==0x417becu&&wm[15].arg[0]==A&&wm[15].eax==0xaf&&wm[15].edx==0xdf&&wm[15].ecx==0xcf);
    CHECK(wm[16].va==0x413ac8u&&wm[16].arg[0]==A+0x222&&wm[16].eax==0xb0&&wm[16].edx==0xe0&&wm[16].ecx==A+0x222);
    CHECK(wm[17].va==0x417d08u&&wm[17].arg[0]==A&&wm[17].eax==0xb1&&wm[17].edx==0xe1&&wm[17].ecx==A+0x222);
    CHECK(lo(p)==0x5eadu&&hi(p)==0xe2&&ecx==A&&r16(0,f-0x18)==0x20);
    /* The player: new(35h) and 4104ec with six slots, count -6. */
    wm_setup();wm_system=MT;wm_new=SYS;wm_slots=SL;wm_system_count=0;wm_vret=VR;
    arena[A+0xa3-BASE]=7;w16(0,MT,4);w32(0,MT+2,0x55667788u);w32(0,A+0x1fa,TP);w32(0,TP,TBL);w32(0,A+0x20e,CW);
    p=es2_weapon_fit_registers(&wm_memory,&wm_graph,A,0x2222u,0x3333u,entry,&ecx);
    CHECK(wm[2].va==0x47749cu&&wm[2].arg[0]==0x35&&wm[3].va==0x4104ecu&&wm[3].count==6&&wm[3].arg[2]==((SYS&0xffff0000u)|4)&&wm[3].context==0x14);
    CHECK(wm[4].va==0x402604u&&wm[4].context==8&&r32(0,COUNTER)==94&&wm[5].va==0x40cb84u&&wm_count==11&&lo(p)==0x5eadu);
}

/* ---- L5: mech_ctor 415bb0, flyer_ctor 4215f4, building_factory
 * 405314, group_ctor 423b34, base_marks 405db0. The image window of the L4
 * tests (WM_IMAGE) holds the globals; ct_call logs every game call like wm_call
 * and answers EAX = A0h+n, EDX = D0h+n, ECX = C0h+n (n = call number from
 * 1) unless scripted below; the fake 49630c writes the saved head 5EADh at
 * the record (f-24h for descriptor 49a080, f-34h for 497556) and clears its
 * context word; memset 49681a fills memory and returns EAX = destination,
 * EDX = count, ECX = 0 as the runtime's does. Expected values are worked out
 * by hand from the contract in mission_load.md. */
#define CT_SCR (BASE+0x6a000u)
static wm_rec ct[32];static unsigned ct_count;static uint32_t ct_record;
static uint32_t ct_new,ct_pool,ct_nth[4],ct_array;static unsigned ct_nth_count;
static void ct_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ct_count<32);wm_rec *k=ct+ct_count++;memset(k,0,sizeof(*k));
    k->va=f-WM_IMAGE<sizeof(wm_image)?f-WM_IMAGE+0x400000u:f;for(unsigned i=0;i<n&&i<7;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(ct_record)k->context=wm_r16(0,ct_record+0x10u);
    if(k->va==0x49630cu){
        ct_record=r->eax==WVA(0x49a080)?CT_SCR+ES2_MECH_CTOR_FRAME-0x24u:CT_SCR+ES2_BUILDING_FACTORY_FRAME-0x34u;
        wm_w32(0,ct_record,0x5eadu);wm_w16(0,ct_record+0x10u,0);r->eax=0x5eadu;return;
    }
    if(k->va==0x49681au){for(uint32_t i=0;i<a[2];++i)((uint8_t *)wm_host(0,a[0]+i))[0]=(uint8_t)a[1];
        r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    r->eax=0xa0u+ct_count;r->edx=0xd0u+ct_count;r->ecx=0xc0u+ct_count;
    switch(k->va){
    case 0x4774f0u:r->eax=ct_new;break;
    case 0x471b30u:r->eax=ct_pool;break;
    case 0x471cb0u:r->eax=ct_nth[ct_nth_count++%4];break;
    case 0x49667cu:if(ct_array)r->eax=ct_array;break;
    }
}
static const es2_weapon_graph ct_graph={WM_IMAGE,CT_SCR,0,0,0,ct_call,0,fake_observe,0};
static void ct_setup(void){wm_setup();ct_count=0;ct_record=0;ct_new=ct_pool=ct_array=0;ct_nth_count=0;}

static void test_mech_ctor(void){
    const uint32_t A=BASE+0x41000u,LIST=BASE+0x42000u,FO=BASE+0x43000u,SH=BASE+0x44000u,FL=BASE+0x45000u,NM=BASE+0x46000u;
    const uint32_t TT=BASE+0x50000u,T=TT+0x21eu,COUNTER=BASE+0x3f000u,f=CT_SCR+ES2_MECH_CTOR_FRAME;
    uint32_t ecx=0;
    /* An AI mech (index 1 of type word 5), one flat object, the second
     * track only, euler_matrix (A+32h = 0). */
    ct_setup();ct_new=FL;ct_pool=FO;
    wm_image[0xa9d08+5]=1;wm_w32(0,WVA(0x4a9d64),TT);
    w16(0,T+0xc0,100);w16(0,T+0x54,0);w16(0,T+0x12,0xffff);w16(0,T+0x1c,2);w16(0,T+0x24,0xffff);
    arena[T+0x72-BASE]=3;arena[T+0x73-BASE]=0xff;w16(0,T+0x50,0);w16(0,T+0x6c,2);
    arena[A+0xa3-BASE]=0;w16(0,A+0x32,0);
    for(unsigned i=0;i<5;++i)w32(0,A+0x12+4*i,0x1000u+i);
    w32(0,A+0x26,7);w32(0,A+0x2a,8);w32(0,A+0x2e,9);
    wm_w32(0,WVA(0x4a9af8),LIST);wm_w16(0,WVA(0x4a9afe),4);
    wm_w32(0,WVA(0x4a968d),0x77777777u);wm_w32(0,WVA(0x4a9698),SH);w32(0,SH+12,0x5157u);
    wm_w32(0,WVA(0x4a9d60),NM);w32(0,NM+8,0x4e414d45u);wm_w32(0,WVA(0x4a0a04),0x5f310000u);wm_w32(0,WVA(0x4a0a08),0x5f320000u);
    uint64_t p=es2_mech_ctor_registers(&wm_memory,&ct_graph,A,0x00010005u,0x12340000u,entry,&ecx);
    CHECK(ct_count==18&&observed==18);
    CHECK(ct[0].va==0x49630cu&&ct[0].eax==WVA(0x49a080)&&ct[0].edx==0xe1&&ct[0].ecx==0xe2);
    /* 4033a4 with ECX = the caller's ESI high word and the type word */
    CHECK(ct[1].va==0x4033a4u&&ct[1].count==2&&ct[1].arg[0]==A&&ct[1].arg[1]==T+0xde&&ct[1].eax==T+0xde&&ct[1].edx==1&&ct[1].ecx==0x12340005u);
    CHECK(r32(0,A)==WVA(0x49a282)&&r16(0,A+0x1a8)==0&&arena[A+0x98-BASE]==1&&arena[A+0xa7-BASE]==1);
    /* the actor list: slot 4, count 5, A+4Bh = 4 */
    CHECK(r32(0,LIST+16)==A&&wm_r16(0,WVA(0x4a9afe))==5&&r16(0,A+0x4b)==4);
    CHECK(r32(0,A+0x1b6)==0&&r32(0,A+0x1b2)==0&&r16(0,A+0x1be)==0xffff&&r16(0,A+0x1bc)==0xffff&&r16(0,A+0x1ba)==0xffff);
    CHECK(ct[2].va==0x40d2ccu&&ct[2].count==3&&ct[2].arg[0]==A+0x206&&ct[2].arg[1]==0x1d&&ct[2].arg[2]==0x16);
    CHECK(ct[2].eax==0xffffffffu&&ct[2].edx==COUNTER&&ct[2].ecx==4);
    CHECK(ct[3].va==0x49667cu&&ct[3].count==5&&ct[3].arg[0]==A+0x258&&ct[3].arg[1]==3&&ct[3].arg[2]==5&&ct[3].arg[3]==1&&ct[3].arg[4]==WVA(0x41c03e));
    CHECK(ct[3].eax==A+0x23e&&ct[3].edx==A+0x258&&ct[3].ecx==0xc3);
    CHECK(r32(0,f-0x28)==A+0x4d&&r32(0,f-0x2c)==A+0x23e&&r32(0,f-0x30)==A+0x2c5&&r32(0,f-0x38)==A+0x2f1&&r32(0,f-0x3c)==A+0x31e);
    CHECK(r16(0,A+0x29c)==0&&r16(0,A+4)==7&&r32(0,A+0x1f2)==T&&r16(0,A+0x292)==0x2710);
    /* 413a90(A+222h, 100): +6 = +8 = 100, +4 = 200h, +2 = +0 = 50 */
    CHECK(r16(0,A+0x228)==100&&r16(0,A+0x22a)==100&&r16(0,A+0x226)==0x200&&r16(0,A+0x224)==50&&r16(0,A+0x222)==50);
    CHECK(arena[A+0xb3-BASE]==1&&r32(0,f-0x40)==A+0x92&&r32(0,A+0x22c)==0&&r32(0,A+0x234)==0);
    /* the second track (word +1Ch = 2): EAX 0 from the skipped first, ECX = A+92h */
    CHECK(ct[4].va==0x402374u&&ct[4].arg[0]==A&&ct[4].arg[1]==((A&0xffff0000u)|2)&&ct[4].eax==0&&ct[4].edx==((A&0xffff0000u)|2)&&ct[4].ecx==A+0x92);
    CHECK(r32(0,A+0x230)==0xa5);
    /* one flat object: new[](4) with EAX = 1, EDX = 0 (the -1 byte + 1) */
    CHECK(ct[5].va==0x4774f0u&&ct[5].arg[0]==4&&ct[5].eax==1&&ct[5].edx==0&&ct[5].ecx==4&&r32(0,A+0x238)==FL&&r16(0,A+0x23c)==1);
    CHECK(ct[6].va==0x471b30u&&ct[6].arg[0]==0x77777777u&&ct[6].eax==0x77777777u&&ct[6].edx==0xd6&&ct[6].ecx==4&&ct[6].context==0);
    CHECK(ct[7].va==0x402188u&&ct[7].arg[0]==FO&&ct[7].arg[1]==0x5157u&&ct[7].eax==SH&&ct[7].edx==0x5157u&&ct[7].ecx==FO&&ct[7].context==0x14);
    CHECK(r32(0,FO)==WVA(0x49a262)&&r16(0,FO+4)==9&&r32(0,FO+0x2e)==0xfffe7960u&&r32(0,FL)==FO&&r16(0,f-0x46)==3);
    /* deciding state: EAX = A+92h, EDX = A+4Dh, ECX = the flat object */
    CHECK(ct[8].va==0x413e50u&&ct[8].arg[0]==A+0x4d&&ct[8].arg[1]==WVA(0x4993a4)&&ct[8].eax==A+0x92&&ct[8].edx==A+0x4d&&ct[8].ecx==FO&&ct[8].context==8);
    CHECK(ct[9].va==0x492becu&&ct[9].arg[0]==0x4e414d45u&&ct[9].arg[1]==0x5f310000u&&ct[9].eax==0x5f310000u&&ct[9].edx==NM&&ct[9].ecx==T);
    CHECK(ct[10].va==0x40d160u&&ct[10].count==1&&ct[10].arg[0]==0xaa&&ct[10].eax==0xaa&&ct[10].edx==0xda&&ct[10].ecx==0xca&&r32(0,A+0x1fa)==0xab);
    CHECK(ct[11].va==0x492becu&&ct[11].arg[1]==0x5f320000u&&ct[11].eax==0xab&&ct[11].edx==0x5f320000u&&ct[11].ecx==0xaa);
    CHECK(ct[12].va==0x40cd88u&&ct[12].arg[0]==0xac&&ct[12].eax==0xac&&ct[12].edx==0xdc&&ct[12].ecx==0xcc&&r32(0,A+0x1f6)==0xad);
    CHECK(ct[13].va==0x40d354u&&ct[13].count==4&&ct[13].arg[0]==A+0x206&&ct[13].arg[1]==A&&ct[13].arg[2]==0xab&&ct[13].arg[3]==0xad);
    CHECK(ct[13].eax==0xab&&ct[13].edx==A+0x206&&ct[13].ecx==0xac);
    CHECK(ct[14].va==0x49681au&&ct[14].arg[0]==A+0x2b9&&ct[14].arg[2]==0x4e&&ct[14].eax==A+0x2b9&&ct[14].edx==0xde&&ct[14].ecx==0);
    CHECK(arena[A+0x2b4-BASE]==0&&arena[A+0x2b7-BASE]==0&&arena[A+0x2b8-BASE]==1&&r32(0,A+0x2d9)==A+0xc);
    CHECK(ct[15].va==0x47ebb8u&&ct[15].arg[0]==A+0xc&&ct[15].arg[1]==A+0x12&&ct[15].eax==A+0x12&&ct[15].edx==A+0xc&&ct[15].ecx==0&&r16(0,A+0x32)==1);
    /* the copy A+12h -> A+2DDh, the translation (7, 8, 9) negated */
    CHECK(r32(0,A+0x2dd)==0x1000u&&r32(0,A+0x2ed)==0x1004u&&r32(0,A+0x2f1)==(uint32_t)-7&&r32(0,A+0x2f5)==(uint32_t)-8&&r32(0,A+0x2f9)==(uint32_t)-9);
    CHECK(ct[16].va==0x47df19u&&ct[16].arg[0]==A+0x2dd&&ct[16].eax==A+0x2dd&&ct[16].edx==A+0x2f1&&ct[16].ecx==8);
    CHECK(ct[17].va==0x480304u&&ct[17].arg[0]==f-0x58&&ct[17].arg[1]==A+0x2dd&&ct[17].arg[2]==A+0x2f1);
    CHECK(ct[17].eax==(uint32_t)-9&&ct[17].edx==f-0x58&&ct[17].ecx==(uint32_t)-8&&r32(0,f-0x58)==(uint32_t)-7&&r32(0,f-0x50)==(uint32_t)-9);
    /* counter 100 +1 +1 -4 +1 */
    CHECK(r32(0,A+0x2bd)==0x3e8&&r16(0,A+0x2d7)==0&&r16(0,A+0x31c)==0&&r32(0,COUNTER)==99);
    CHECK(lo(p)==A&&hi(p)==COUNTER&&ecx==0x5eadu);
    /* The player, flying type (index 0), no flats, all three tracks (the
     * first also clears the nodes of words +12h/+14h), no euler_matrix. */
    const uint32_t X=BASE+0x47000u,Y=BASE+0x47100u,Z=BASE+0x47200u,ARR=BASE+0x47300u,N0=BASE+0x47400u,N1=BASE+0x47500u;
    ct_setup();ct_new=FL;ct_pool=FO;
    wm_image[0xa9d08+5]=0;wm_w32(0,WVA(0x4a9d64),T);
    w16(0,T+0xc0,0xfffd);w16(0,T+0x54,1);w16(0,T+0x12,1);w16(0,T+0x14,0);w16(0,T+0x1c,3);w16(0,T+0x24,4);
    arena[T+0x72-BASE]=0x80;w16(0,T+0x50,1);w16(0,T+0x6c,0);
    arena[A+0xa3-BASE]=1;w16(0,A+0x32,5);w32(0,A+0x34,X);w32(0,X+4,Y);w32(0,Y+0x2a,Z);w32(0,Z+6,ARR);w32(0,ARR,N0);w32(0,ARR+4,N1);
    w16(0,N0+6,0x1111);w16(0,N1+6,0x2222);
    wm_w32(0,WVA(0x4a9af8),LIST);wm_w16(0,WVA(0x4a9afe),0);wm_w32(0,WVA(0x4a9d60),NM);w32(0,NM,0x11);
    p=es2_mech_ctor_registers(&wm_memory,&ct_graph,A,0xffff0005u,0,entry,&ecx);
    CHECK(ct_count==16&&ct[1].ecx==5&&r16(0,A+0x224)==0xfffe&&arena[A+0xb3-BASE]==0xcc);   /* -3 SAR 1 = -2 */
    CHECK(ct[4].va==0x402374u&&ct[4].arg[1]==((A&0xffff0000u)|1)&&ct[4].ecx==0xfffffffeu&&r16(0,N0+6)==0&&r16(0,N1+6)==0);
    CHECK(ct[5].va==0x402374u&&ct[5].arg[1]==((Z&0xffff0000u)|3)&&ct[5].eax==N0&&ct[5].ecx==ARR);
    CHECK(ct[6].va==0x402374u&&ct[6].arg[1]==4&&ct[6].eax==4&&ct[6].edx==0xd6&&r32(0,A+0x234)==0xa7);
    /* no flats: no new[]; the player-fly state with ECX = A+4Dh */
    CHECK(ct[7].va==0x413e50u&&ct[7].arg[1]==WVA(0x499420)&&ct[7].eax==A+0x92&&ct[7].edx==T&&ct[7].ecx==A+0x4d);
    CHECK(ct[13].va==0x49681au&&ct[14].va==0x47df19u&&ct[15].va==0x480304u&&r16(0,A+0x32)==5);
    CHECK(wm_r16(0,WVA(0x4a9afe))==1&&r32(0,LIST)==A&&lo(p)==A&&ecx==0x5eadu);
}
static void test_flyer_ctor(void){
    const uint32_t F=BASE+0x41000u,LIST=BASE+0x42000u,TT=BASE+0x50000u,T=TT+0x70u,COUNTER=BASE+0x3f000u,f=CT_SCR+ES2_FLYER_CTOR_FRAME;
    uint32_t ecx=0;
    ct_setup();
    wm_image[0xa9dcc+2]=1;wm_w32(0,WVA(0x4a9df4),TT);w32(0,T+0x36,0x5a5a);w16(0,T+4,0x77);w32(0,T+0x2e,0x2e2e);w32(0,T+0x32,0x3232);
    wm_w32(0,WVA(0x4a9af8),LIST);wm_w16(0,WVA(0x4a9afe),2);
    w16(0,F+0x32,0);w32(0,F+0x26,1);w32(0,F+0x2a,2);w32(0,F+0x2e,3);
    uint64_t p=es2_flyer_ctor_registers(&wm_memory,&ct_graph,F,0x55550002u,entry,&ecx);
    CHECK(ct_count==8);
    /* 403368(F, [T+36h]) with EDX = 14 x index, ECX = the table */
    CHECK(ct[0].va==0x403368u&&ct[0].arg[0]==F&&ct[0].arg[1]==0x5a5a&&ct[0].eax==0x5a5a&&ct[0].edx==14&&ct[0].ecx==TT);
    CHECK(r32(0,LIST+8)==F&&wm_r16(0,WVA(0x4a9afe))==3&&r16(0,F+0x4b)==2&&r32(0,F)==WVA(0x49a5e0));
    CHECK(ct[1].va==0x40d2ccu&&ct[1].arg[0]==F+0x200&&ct[1].arg[1]==1&&ct[1].arg[2]==1&&ct[1].eax==0xffffffffu&&ct[1].edx==COUNTER&&ct[1].ecx==2);
    CHECK(r16(0,F+4)==6&&r32(0,F+0x1fa)==T&&r16(0,F+0x1a8)==2&&r16(0,F+0x233)==0x77&&r16(0,F+0x231)==0&&r16(0,F+0x235)==0);
    CHECK(ct[2].va==0x40d354u&&ct[2].arg[0]==F+0x200&&ct[2].arg[1]==F&&ct[2].arg[2]==0x2e2e&&ct[2].arg[3]==0x3232);
    CHECK(ct[2].eax==0x2e2e&&ct[2].edx==F+0x200&&ct[2].ecx==T);
    CHECK(ct[3].va==0x413e50u&&ct[3].arg[0]==F+0x4d&&ct[3].arg[1]==WVA(0x499cf8)&&ct[3].eax==0xa3&&ct[3].edx==0xd3&&ct[3].ecx==F+0x4d);
    CHECK(ct[4].va==0x49681au&&ct[4].arg[0]==F+0x243&&ct[4].eax==F+0x243&&ct[4].edx==0xd4&&ct[4].ecx==0xc4&&r32(0,F+0x263)==F+0xc);
    CHECK(ct[5].va==0x47ebb8u&&ct[5].arg[0]==F+0xc&&ct[5].eax==F+0x12&&ct[5].edx==F+0xc&&ct[5].ecx==0&&r16(0,F+0x32)==1);
    CHECK(ct[6].va==0x47df19u&&ct[6].eax==F+0x267&&ct[6].edx==F+0x27b&&ct[6].ecx==2&&r32(0,F+0x27b)==(uint32_t)-1&&r32(0,F+0x283)==(uint32_t)-3);
    CHECK(ct[7].va==0x480304u&&ct[7].arg[0]==f-0xc&&ct[7].arg[1]==F+0x267&&ct[7].arg[2]==F+0x27b&&ct[7].eax==(uint32_t)-3&&ct[7].edx==F+0x27b&&ct[7].ecx==f-0xc);
    CHECK(r32(0,f-8)==(uint32_t)-2&&r32(0,F+0x247)==0x3e8&&r16(0,F+0x261)==0x200&&r32(0,COUNTER)==102);
    CHECK(lo(p)==F&&hi(p)==0xd8&&ecx==0xc8);
}
static void test_building_factory(void){
    const uint32_t R=BASE+0x40000u,O=BASE+0x41000u,TB=BASE+0x48000u,CFG=BASE+0x48100u,PR=BASE+0x48200u,PW=BASE+0x48300u;
    const uint32_t SI=BASE+0x48400u,FR=BASE+0x48500u,S1=BASE+0x49000u,S2=BASE+0x49100u,MD=BASE+0x49200u,LIST=BASE+0x42000u;
    const uint32_t COUNTER=BASE+0x3f000u,f=CT_SCR+ES2_BUILDING_FACTORY_FRAME;
    uint32_t ecx=0;
    /* Type 34 (LC_BASE, case 4), the record word +32h = 40: two tracks,
     * two parts at 60% of their configuration words (100, -20). */
    ct_setup();ct_pool=O;ct_new=PW;ct_array=PR;
    wm_image[0x5366+34]=4;w16(0,R,34);w16(0,R+0x32,40);
    wm_w32(0,WVA(0x4a959c),TB-60u*34u);wm_w32(0,WVA(0x4a9574),S1);wm_w32(0,WVA(0x4a957c),S2);wm_w32(0,WVA(0x4a95a0),0x6666u);
    w16(0,TB+2,1);w32(0,S2+4,0x5e5e);w16(0,TB+6,2);w16(0,TB+0x20,0x0101);w16(0,TB+0x22,0x0202);w16(0,TB+0x1e,1);
    w16(0,TB+0x12,2);w32(0,TB+0x14,CFG);w16(0,CFG,100);w16(0,CFG+0x1e,0xffec);w16(0,TB+0x26,0x2626);
    wm_w32(0,WVA(0x4a9af8),LIST);wm_w16(0,WVA(0x4a9afe),0);
    uint64_t p=es2_building_factory_registers(&wm_memory,&ct_graph,R,0xd1d1d1d1u,entry,&ecx);
    CHECK(ct_count==11);
    CHECK(ct[0].va==0x49630cu&&ct[0].eax==WVA(0x497556)&&ct[0].edx==0xe1&&ct[0].ecx==0xe2);
    /* pool_alloc with EAX = the pool, EDX = the first table, ECX = entry */
    CHECK(ct[1].va==0x471b30u&&ct[1].arg[0]==0x6666u&&ct[1].eax==0x6666u&&ct[1].edx==S1&&ct[1].ecx==0xe2&&ct[1].context==0);
    CHECK(ct[2].va==0x403368u&&ct[2].arg[0]==O&&ct[2].arg[1]==0x5e5e&&ct[2].eax==0x5e5e&&ct[2].edx==O&&ct[2].ecx==0x5e5e&&ct[2].context==0x74);
    CHECK(r32(0,f-0x38)==S1&&r32(0,f-0x14)==O&&r32(0,f-0x88)==0x5e5e&&r32(0,f-0x8c)==O+0x4d&&r32(0,f-0x90)==O+0x92&&r32(0,f-0x94)==O+0x92);
    CHECK(r32(0,LIST)==O&&r16(0,O+0x4b)==0&&arena[O+0x98-BASE]==1&&arena[O+0xa7-BASE]==1&&r16(0,O+4)==5&&r16(0,O+0x1a8)==1);
    /* 49667c(O+209h, 1Fh, 3, 1, 4069c5) with EAX = the counter pointer */
    CHECK(ct[3].va==0x49667cu&&ct[3].arg[0]==O+0x209&&ct[3].arg[1]==0x1f&&ct[3].arg[2]==3&&ct[3].arg[3]==1&&ct[3].arg[4]==WVA(0x4069c5));
    CHECK(ct[3].eax==COUNTER&&ct[3].edx==O&&ct[3].ecx==O+0x209&&ct[3].context==0x74&&r32(0,O)==WVA(0x497784));
    /* the tracks: rates 101h and 202h, the second sought to 6000 */
    CHECK(ct[4].va==0x402374u&&ct[4].arg[0]==O&&ct[4].arg[1]==0&&ct[4].eax==TB+0x20&&ct[4].edx==O+0x1f9&&ct[4].ecx==2&&ct[4].context==0x68);
    CHECK(ct[5].va==0x4796d8u&&ct[5].arg[0]==0xa5&&ct[5].arg[1]==0x0101&&ct[5].eax==O+0x1f9&&ct[5].edx==0xa5&&ct[5].ecx==0x0101);
    CHECK(ct[6].va==0x402374u&&ct[6].arg[1]==1&&ct[6].eax==0xa6&&ct[6].edx==0xd6&&ct[6].ecx==2);
    CHECK(ct[7].va==0x4796d8u&&ct[7].arg[0]==0xa7&&ct[7].arg[1]==0x0202&&ct[7].eax==O+0x1fd);
    CHECK(ct[8].va==0x479344u&&ct[8].arg[0]==0xa7&&ct[8].arg[1]==1&&ct[8].arg[2]==0x1770&&ct[8].eax==0xa7&&ct[8].edx==0xd8&&ct[8].ecx==O+0x1fd);
    CHECK(r32(0,O+0x1f9)==0xa5&&r32(0,O+0x1fd)==0xa7&&arena[O+0xb7-BASE]==1);
    /* new[](4) with EAX = O+92h; the part words all 1; the part records */
    CHECK(ct[9].va==0x4774f0u&&ct[9].arg[0]==4&&ct[9].eax==O+0x92&&ct[9].edx==4&&ct[9].ecx==0xc9&&r32(0,O+0x201)==PW);
    CHECK(r16(0,PW)==1&&r16(0,PW+2)==1&&r16(0,PW+4)==0xcccc);
    CHECK(ct[10].va==0x49667cu&&ct[10].arg[0]==0&&ct[10].arg[1]==0xb&&ct[10].arg[2]==2&&ct[10].arg[3]==1&&ct[10].arg[4]==WVA(0x4069e5));
    CHECK(ct[10].eax==PW&&ct[10].edx==0xda&&ct[10].ecx==2&&r32(0,O+0x205)==PR);
    /* (100 - 40) x 100 / 100 = 60, (100 - 40) x -20 / 100 = -12 */
    CHECK(r16(0,PR+5)==0&&r16(0,PR)==60&&r16(0,PR+0x10)==0&&r16(0,PR+0xb)==0xfff4&&r16(0,PR+0x16)==0xcccc);
    CHECK(arena[O+0x99-BASE]==0xcc&&r16(0,O+0x1f7)==0x2626&&r32(0,O+0x1f2)==TB);
    /* counter 100 +1 +1 +1 -8 */
    CHECK(r32(0,COUNTER)==95&&lo(p)==O&&hi(p)==0x5eadu&&ecx==R);
    /* A BASE (type 0, case 2) with the record word +32h = 0: the
     * configuration words are copied, frame word 3 of the shape instance
     * set, the model of word +4 stored. */
    ct_setup();ct_pool=O;ct_new=PW;ct_array=PR;
    wm_image[0x5366+0]=2;w16(0,R,0);w16(0,R+0x32,0);
    wm_w32(0,WVA(0x4a959c),TB);wm_w32(0,WVA(0x4a957c),S2);w32(0,S2,0x5e);
    w16(0,TB+2,0);w16(0,TB+4,1);w16(0,TB+6,0);w16(0,TB+0x1e,0);w16(0,TB+0x12,2);w32(0,TB+0x14,CFG);
    w16(0,CFG,100);w16(0,CFG+2,3);w16(0,CFG+0x1e,0x55);w16(0,CFG+0x20,0xffff);
    w32(0,O+0x34,SI);w32(0,SI+8,FR);wm_w32(0,WVA(0x4a9584),MD);w32(0,MD+4,0x4d4d4d4du);
    wm_w32(0,WVA(0x4a9af8),LIST);wm_w16(0,WVA(0x4a9afe),0);
    p=es2_building_factory_registers(&wm_memory,&ct_graph,R,0,entry,&ecx);
    CHECK(ct_count==5&&ct[1].eax==0&&ct[1].edx==0&&ct[2].va==0x403368u&&ct[2].arg[1]==0x5e&&ct[2].context==0x44);
    CHECK(ct[3].va==0x4774f0u&&ct[3].context==0x38&&ct[4].va==0x49667cu&&ct[4].arg[2]==2&&ct[4].ecx==2);
    CHECK(r32(0,O)==WVA(0x497940)&&arena[O+0xa5-BASE]==1&&r32(0,f-0x6c)==O+0x92&&r32(0,COUNTER)==95);
    CHECK(r16(0,PR+5)==0&&r16(0,PR)==100&&r16(0,PR+0x10)==0&&r16(0,PR+0xb)==0x55&&r16(0,FR+6)==1&&r16(0,FR)==0xcccc);
    CHECK(arena[O+0x99-BASE]==1&&arena[O+0x96-BASE]==0&&r32(0,SI+4)==0x4d4d4d4du&&ecx==MD);
    CHECK(lo(p)==O&&hi(p)==0x5eadu);
    /* Case 0 (type 10): no object; the caller's EDI is the object. */
    ct_setup();ct_new=PW;ct_array=PR;
    w16(0,R,10);w16(0,R+0x32,0xffff);wm_w32(0,WVA(0x4a959c),TB-600u);
    w16(0,TB+6,0);w16(0,TB+0x12,1);w16(0,TB+0x1e,0);w32(0,TB+0x14,CFG);
    p=es2_building_factory_registers(&wm_memory,&ct_graph,R,O,entry,&ecx);
    CHECK(ct_count==3&&ct[1].va==0x4774f0u&&ct[1].eax==TB+0x20&&ct[1].edx==2&&ct[1].ecx==0&&ct[1].context==0);
    CHECK(r32(0,O+0x201)==PW&&r16(0,PR)==0&&r16(0,PR+5)==0&&lo(p)==O&&hi(p)==0x5eadu&&ecx==R&&r32(0,COUNTER)==100);
}
static void test_group_ctor(void){
    const uint32_t G=BASE+0x40000u,R=BASE+0x40100u,OR=BASE+0x40200u,O0=BASE+0x40300u,MB=BASE+0x40400u;
    uint32_t ecx=0;
    /* Class 0 with members in slots 1 and 3; the first order's route. */
    ct_setup();ct_new=MB;ct_nth[0]=0x11110000u;ct_nth[1]=0x22220000u;
    w16(0,R+0x28,0);arena[R-BASE+0x6e]=0x6e;w16(0,R+0x2a,0x2a2a);
    for(unsigned i=0;i<20;++i)w16(0,R+0x32+2*i,0xffff);
    w16(0,R+0x34,5);w16(0,R+0x38,7);
    for(unsigned i=0;i<20;++i)w16(0,R+0x72+2*i,(uint16_t)(0x100+i));
    for(unsigned i=0;i<10;++i)w32(0,OR+4*i,i?0:O0);
    w32(0,O0+8,0x80808080u);
    wm_w32(0,WVA(0x4a9b7a),0x9b7a);
    uint64_t p=es2_group_ctor_registers(&wm_memory,&ct_graph,G,R,0x7777,OR,0xabcd1234u,0x5555,entry,&ecx);
    CHECK(ct_count==6);
    CHECK(r16(0,G)==0&&arena[G+0x12-BASE]==0x6e&&r16(0,G+0xa)==0x1234&&r16(0,G+2)==0x2a2a&&r32(0,G+0x14)==0x7777&&r32(0,G+0x18)==0x5555);
    CHECK(r32(0,G+0x6c)==0&&arena[G+0x13-BASE]==0&&arena[G+0x70-BASE]==0&&arena[G+0x79-BASE]==0&&arena[G+0x7a-BASE]==0xcc);
    CHECK(ct[0].va==0x49681au&&ct[0].arg[0]==G+0x70&&ct[0].arg[2]==0xa&&ct[0].eax==0x5555&&ct[0].edx==0&&ct[0].ecx==G+0x70);
    CHECK(r16(0,G+0x1c)==0x100&&r16(0,G+0x2e)==0x109&&r16(0,G+0x30)==0x10a&&r16(0,G+0x42)==0x113);
    CHECK(r32(0,G+0x44)==O0&&r32(0,G+0x48)==0&&r32(0,G+6)==0x80808080u&&r16(0,G+4)==0&&r16(0,G+0x10)==2);
    CHECK(ct[1].va==0x4774f0u&&ct[1].arg[0]==8&&ct[1].eax==20&&ct[1].edx==R+0x58&&ct[1].ecx==8&&r32(0,G+0xc)==MB);
    /* pool_nth(4a9b7a, 5): EAX = the class's -1 with the slot word, EDX =
     * new[]'s EDX with the slot word */
    CHECK(ct[2].va==0x471cb0u&&ct[2].arg[0]==0x9b7a&&ct[2].arg[1]==5&&ct[2].eax==0xffff0005u&&ct[2].edx==5&&ct[2].ecx==0x9b7a);
    CHECK(ct[3].va==0x417aa8u&&ct[3].count==3&&ct[3].arg[0]==0x11110000u&&ct[3].arg[1]==G&&ct[3].arg[2]==1&&ct[3].eax==0x11110000u&&ct[3].edx==0&&ct[3].ecx==MB);
    CHECK(ct[4].va==0x471cb0u&&ct[4].arg[1]==7&&ct[4].eax==7&&ct[4].edx==7&&ct[4].ecx==0x9b7a);
    CHECK(ct[5].va==0x417aa8u&&ct[5].arg[0]==0x22220000u&&ct[5].arg[2]==3&&ct[5].edx==1&&ct[5].ecx==MB);
    CHECK(r32(0,MB)==0x11110000u&&r32(0,MB+4)==0x22220000u&&lo(p)==0xa6&&hi(p)==0xd6&&ecx==0xc6);
    /* Class 2 with one member (slot 7) and no first order, then class 3. */
    ct_setup();ct_new=MB;ct_nth[0]=0x33330000u;
    w16(0,R+0x28,2);w16(0,R,0x8001);w16(0,R+0x40,2);
    for(unsigned i=0;i<10;++i)w32(0,OR+4*i,0);
    wm_w32(0,WVA(0x4a95a0),0x95a0);
    p=es2_group_ctor_registers(&wm_memory,&ct_graph,G,R,0,OR,0,0,entry,&ecx);
    CHECK(ct_count==4&&ct[1].arg[0]==4&&ct[2].arg[0]==0x95a0&&ct[2].arg[1]==2&&r32(0,G+6)==0xccccccccu);
    CHECK(ct[3].va==0x405c3cu&&ct[3].count==4&&ct[3].arg[0]==0x33330000u&&ct[3].arg[1]==G&&ct[3].arg[2]==7&&ct[3].arg[3]==0xffff8001u);
    CHECK(ct[3].eax==0x33330000u&&ct[3].edx==R&&ct[3].ecx==0xffff8001u&&r32(0,MB)==0x33330000u);
    ct_setup();ct_new=MB;w16(0,R+0x28,3);
    for(unsigned i=0;i<10;++i)w32(0,OR+4*i,0);
    p=es2_group_ctor_registers(&wm_memory,&ct_graph,G,R,0,OR,0,0,entry,&ecx);
    CHECK(ct_count==2&&ct[1].arg[0]==0&&lo(p)==1&&hi(p)==0xd2&&ecx==0);
}
static void test_base_marks(void){
    const uint32_t B=BASE+0x40000u,G=BASE+0x40100u,BT=BASE+0x40200u;
    uint32_t ecx=0;
    ct_setup();
    w32(0,B+0x45,G);w16(0,G+2,3);wm_w32(0,WVA(0x4a95ac),BT);w32(0,BT+84+8,0x88);w32(0,BT+84+0x14,0x14);w32(0,BT+84+0x18,0x18);
    wm_w32(0,WVA(0x4a0be0),0xbe0);
    uint64_t p=es2_base_marks_registers(&wm_memory,&ct_graph,B,entry,&ecx);
    CHECK(ct_count==1&&ct[0].va==0x47136cu&&ct[0].count==5&&ct[0].arg[0]==0xbe0&&ct[0].arg[1]==B+0x26&&ct[0].arg[2]==0x88);
    CHECK(ct[0].arg[3]==0x18&&ct[0].arg[4]==0x14&&ct[0].eax==0xbe0&&ct[0].edx==B+0x26&&ct[0].ecx==0x88);
    CHECK(lo(p)==0xa1&&hi(p)==0xd1&&ecx==0xc1);
    /* mask -1: no call; EAX = the record, EDX = B, ECX = -1 */
    ct_setup();w32(0,B+0x45,G);w16(0,G+2,0xffff);wm_w32(0,WVA(0x4a95ac),BT);w32(0,BT-28+8,0xffffffffu);
    p=es2_base_marks_registers(&wm_memory,&ct_graph,B,entry,&ecx);
    CHECK(ct_count==0&&lo(p)==BT-28&&hi(p)==B&&ecx==0xffffffffu);
}

/* The actor load 4253d8 over a small script (one coordinate, one
 * route of two points, one trigger record; one authored mech with a
 * coordinate and the MEC player; an unselected flyer; one building with
 * the default position; one order, one group (group 0), one objective;
 * two runtime triggers of types 7 and 3). The logging call function
 * numbers the calls k = 0, 1, ... and returns EAX = A1h+k, EDX = D1h+k,
 * ECX = C1h+k unless scripted: 49630c links the record at f-34h (saved head
 * 5EADh, context 0), 47ad40 stores the stream vtable, the reads (47af98 and
 * the virtual +18h at 4D0018h) copy the next bytes of the script, pool_alloc
 * gives the mech objects in turn, mech_ctor stores the type record at
 * A+1F2h, terrain_height gives 1234h then 2222h, the factory the building,
 * new[] the order and objective blocks, pool_prev the building then 0, and
 * memcpy copies. Expected values are worked out by hand from the contract
 * in mission_load.md. */
#define AL_SCR (BASE+0x6c000u)
static uint8_t al_file[1024];static unsigned al_file_size,al_pos,al_pool,al_terrain,al_new;
static wm_rec al[64];static unsigned al_count;static uint32_t al_record;
static uint32_t al_m0,al_m1,al_b0,al_tr,al_vt,al_ob,al_objs;
static void al_put16(uint16_t w){al_file[al_file_size++]=(uint8_t)w;al_file[al_file_size++]=(uint8_t)(w>>8);}
static uint8_t *al_bytes(unsigned n,uint8_t fill){uint8_t *p=al_file+al_file_size;memset(p,fill,n);al_file_size+=n;return p;}
static void al_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(al_count<64);const unsigned k1=al_count+1u;wm_rec *k=al+al_count++;memset(k,0,sizeof(*k));
    k->va=f-WM_IMAGE<sizeof(wm_image)?f-WM_IMAGE+0x400000u:f;for(unsigned i=0;i<n&&i<7;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(al_record)k->context=wm_r16(0,al_record+0x10u);
    if(k->va==0x49630cu){al_record=AL_SCR+ES2_ACTOR_LOAD_FRAME-0x34u;wm_w32(0,al_record,0x5eadu);wm_w16(0,al_record+0x10u,0);r->eax=0x5eadu;return;}
    if(k->va==0x49680eu){for(uint32_t i=0;i<a[2];++i)((uint8_t *)wm_host(0,a[0]+i))[0]=((uint8_t *)wm_host(0,a[1]+i))[0];
        r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    r->eax=0xa0u+k1;r->edx=0xd0u+k1;r->ecx=0xc0u+k1;
    switch(k->va){
    case 0x47ad40u:wm_w32(0,a[0],al_vt);break;
    case 0x47af98u:case 0x4d0018u:for(uint32_t i=0;i<a[1];++i)((uint8_t *)wm_host(0,a[2]+i))[0]=al_pos<al_file_size?al_file[al_pos++]:0;break;
    case 0x471b30u:r->eax=al_pool++?al_m1:al_m0;break;
    case 0x415bb0u:wm_w32(0,a[0]+0x1f2u,al_tr);break;
    case 0x46e180u:r->eax=al_terrain++?0x2222u:0x1234u;break;
    case 0x405314u:r->eax=al_b0;break;
    case 0x4774f0u:r->eax=al_new++?al_objs:al_ob;break;
    case 0x471c70u:r->eax=a[1]?0:al_b0;break;
    }
}
static const es2_weapon_graph al_graph={WM_IMAGE,AL_SCR,0,0,0,al_call,0,fake_observe,0};
static void test_actor_load(void){
    const uint32_t f=AL_SCR+ES2_ACTOR_LOAD_FRAME,COUNTER=BASE+0x3f000u;
    const uint32_t M0=BASE+0x41000u,M1=BASE+0x41400u,B0=BASE+0x43000u,BVT=BASE+0x44000u,TR=BASE+0x45000u,MEC=BASE+0x46000u;
    const uint32_t COORD=BASE+0x47000u,ANG=BASE+0x47100u,ROUTES=BASE+0x47200u,TRIG=BASE+0x47400u,GA=BASE+0x48000u;
    const uint32_t OB=BASE+0x49000u,OBJS=BASE+0x49800u,VT=BASE+0x4a000u,TERRAIN=0x7e77a1u,MPOOL=0x9b7a9b7au,BPOOL=0x95a095a0u;
    uint32_t ecx=0;
    wm_setup();al_count=0;al_record=0;al_pos=0;al_pool=0;al_terrain=0;al_new=0;
    al_m0=M0;al_m1=M1;al_b0=B0;al_tr=TR;al_vt=VT;al_ob=OB;al_objs=OBJS;
    /* the trigger case bytes at 426492 (types 0..10), the statics unset */
    static const uint8_t cases[11]={5,5,5,5,5,5,5,4,3,2,1};memcpy(wm_image+0x26492,cases,11);
    w32(0,VT+0x18,0x4d0018u);w32(0,B0,BVT);w32(0,BVT+0x10,0x4d0010u);
    w16(0,TR+0x16,100);w16(0,TR+0x50,0);
    w16(0,MEC,0x0101);w16(0,MEC+4,7);
    w32(0,COORD,1000);w32(0,COORD+4,2000);w32(0,COORD+8,3000);w16(0,ANG+4,0x3333);
    w16(0,TRIG,7);w32(0,TRIG+0x36,3);w16(0,TRIG+58,3);w32(0,TRIG+58+0x36,9);
    wm_w32(0,WVA(0x4a09f0),0x9f09f0u);wm_w32(0,WVA(0x4a9b7a),MPOOL);wm_w32(0,WVA(0x4a95a0),BPOOL);wm_w32(0,WVA(0x4a0be0),TERRAIN);
    wm_w16(0,WVA(0x4aaa5e),1);wm_w32(0,WVA(0x4aaa60),MEC);wm_w16(0,WVA(0x4aaa64),0);
    wm_w32(0,WVA(0x4a9e0c),COORD);wm_w32(0,WVA(0x4a9e10),ANG);wm_w32(0,WVA(0x4a9e18),ROUTES);wm_w32(0,WVA(0x4a9e28),TRIG);
    wm_w16(0,WVA(0x4a9e24),2);wm_w32(0,WVA(0x4a9e00),GA);wm_w16(0,WVA(0x4a9e04),1);
    for(unsigned i=0;i<4;++i)wm_w32(0,WVA(0x4aa660)+4*i,0x57570000u+i);
    wm_image[0xaa72a]=1;wm_image[0xaa72b]=1;wm_image[0xaa856]=0;wm_image[0xaa8ba]=1;wm_image[0xaa9d2]=9;
    wm_image[0xaa78e]=0;wm_image[0xaa78f]=1;
    /* the script: header; one coordinate; no angles; one route of two
     * points; no regions; one trigger; no timers */
    al_file_size=0;al_bytes(20,0x11);
    al_put16(1);al_bytes(12,0x22);al_put16(0);al_put16(1);al_put16(2);al_bytes(4,0x33);al_put16(0);
    al_put16(1);al_bytes(74,0x44);al_put16(0);
    /* one authored mech: word 0 = 1, +2 = 3, type 5, coordinate 0, angle
     * -1, triggers 0 and -1, damage word 55h */
    al_put16(1);uint8_t *R=al_bytes(134,0);
    R[0]=1;R[2]=3;R[0x28]=5;R[0x3e]=0;R[0x40]=0xff;R[0x41]=0xff;R[0x80]=0;R[0x82]=0xff;R[0x83]=0xff;R[0x84]=0x55;
    for(unsigned i=0;i<20;++i){R[0x42+i]=(uint8_t)(0x60+i);R[0x56+i]=(uint8_t)(0x80+i);}
    /* one flyer (not selected); one building: type 22h, no coordinate,
     * angle 2, triggers 1 and -1 */
    al_put16(1);al_bytes(92,0x55);
    al_put16(1);uint8_t *Bq=al_bytes(52,0);Bq[0]=0x22;Bq[2]=0xff;Bq[3]=0xff;Bq[4]=2;Bq[0x2e]=1;Bq[0x30]=0xff;Bq[0x31]=0xff;
    /* one order: words 0A0Ah, 0B0Bh, coordinate 0, route -1, scope 1,
     * index 0, trigger -1 */
    al_put16(1);uint8_t *O=al_bytes(14,0);O[0]=O[1]=0x0a;O[2]=O[3]=0x0b;O[6]=O[7]=0xff;O[8]=1;O[0xc]=O[0xd]=0xff;
    /* one group record (group 0): class 0, coordinate -1, angle -1, the
     * first order 0, trigger -1 */
    al_put16(1);uint8_t *G=al_bytes(156,0xff);G[0]=G[1]=0;G[0x28]=G[0x29]=0;G[0x5a]=G[0x5b]=0;
    /* one objective: words 0C0Ch, 0D0Dh, scope 0, index 2, coordinate -1,
     * route 0, strings from index 1, the two 20-byte blocks */
    al_put16(1);uint8_t *J=al_bytes(54,0);J[0]=J[1]=0x0c;J[2]=J[3]=0x0d;J[6]=2;J[8]=J[9]=0xff;J[0xc]=1;
    for(unsigned i=0;i<20;++i){J[14+i]=(uint8_t)(0xe0+i);J[0x22+i]=(uint8_t)(0xf0+i);}
    uint64_t p=es2_actor_load_registers(&wm_memory,&al_graph,entry,&ecx);
    CHECK(al_count==54&&observed==54&&slot_seen==13);
    /* the statics and the name, the stream, the header */
    CHECK(al[0].va==0x49630cu&&al[0].eax==WVA(0x49a8b5)&&al[0].edx==0xe1&&al[0].ecx==0xe2);
    CHECK(wm_r32(0,WVA(0x49a871))==0&&wm_r32(0,WVA(0x49a875))==0&&wm_r32(0,WVA(0x49a879))==0xffff8000u&&wm_image[0x9a87d]==1);
    CHECK(wm_r16(0,WVA(0x49a87e))==0x8000&&wm_r16(0,WVA(0x49a880))==0x8000&&wm_r16(0,WVA(0x49a882))==0&&wm_image[0x9a884]==1);
    CHECK(al[1].va==0x45ee00u&&al[1].count==1&&al[1].arg[0]==WVA(0x49aa3e)&&al[1].eax==0x9f09f0u&&al[1].edx==0&&al[1].ecx==0&&al[1].context==8);
    CHECK(al[2].va==0x492b90u&&al[2].count==2&&al[2].arg[0]==0xa2&&al[2].arg[1]==0x9f09f0u&&al[2].eax==0xa2&&al[2].edx==0xd2&&al[2].ecx==WVA(0x49aa3e));
    CHECK(al[3].va==0x47ad40u&&al[3].arg[0]==f-0xc&&al[3].arg[1]==0xa3&&al[3].eax==0xa3&&al[3].edx==f-0xc&&al[3].ecx==0xc3);
    CHECK(al[4].va==0x47af98u&&al[4].arg[0]==f-0xc&&al[4].arg[1]==0x14&&al[4].arg[2]==f-0x8c4&&al[4].eax==f-0xc&&al[4].ecx==f-0x8c4);
    CHECK(wm_r16(0,WVA(0x4a9e4c))==0xffff);
    /* the skipped lists: the coordinate count (vtable in EAX), 12 bytes
     * with ECX = the count, angles, routes, the route's points (EAX = the
     * count word), regions, triggers, timers */
    CHECK(al[5].va==0x4d0018u&&al[5].arg[1]==2&&al[5].arg[2]==f-0x36&&al[5].eax==VT&&al[5].edx==f-0x36&&al[5].ecx==f-0xc);
    CHECK(al[6].va==0x47af98u&&al[6].arg[1]==12&&al[6].eax==12&&al[6].edx==f-0xc&&al[6].ecx==1);
    CHECK(al[7].va==0x4d0018u&&al[7].eax==f-0xc&&al[7].edx==VT&&al[7].ecx==f-0x36&&al[8].va==0x4d0018u);
    CHECK(al[9].va==0x4d0018u&&al[9].arg[2]==f-0x44&&al[9].eax==f-0x44&&al[9].edx==f-0xc&&al[9].ecx==VT);
    CHECK(al[10].va==0x47af98u&&al[10].arg[1]==4&&al[10].eax==f-0x8c4&&al[10].edx==4&&al[10].ecx==f-0xc);
    CHECK(al[11].va==0x4d0018u&&al[11].eax==f-0x36&&al[12].va==0x4d0018u&&al[12].eax==VT&&al[12].edx==f-0x36);
    CHECK(al[13].va==0x47af98u&&al[13].arg[1]==0x4a&&al[13].eax==f-0x8c4&&al[13].edx==f-0xc&&al[13].ecx==0xcd);
    CHECK(al[14].va==0x4d0018u&&al[14].arg[2]==f-0x42&&al[14].ecx==f-0x42);
    CHECK(al[15].va==0x40f998u&&al[15].count==0&&al[15].eax==0xaf&&al[15].edx==0xdf&&al[15].ecx==0xcf);
    CHECK(al[16].va==0x4d0018u&&al[16].arg[2]==f-0x38&&al[16].ecx==f-0x38);
    /* the authored mech: its record, pool_alloc, mech_ctor inside context
     * 20h with the type in the object's low word, the ground height 1234h
     * + 100 at the coordinate, the default angles, weapon_fit over the
     * record's words, the triggers, action_copy, initial_damage(55h) */
    CHECK(al[17].va==0x47af98u&&al[17].arg[1]==0x86&&al[17].arg[2]==f-0x964&&al[17].eax==f-0xc&&al[17].edx==MEC&&al[17].ecx==f-0x964);
    CHECK(al[18].va==0x471b30u&&al[18].arg[0]==MPOOL&&al[18].eax==MPOOL&&al[18].edx==0xe2&&al[18].ecx==0xd2&&al[18].context==8);
    CHECK(al[19].va==0x415bb0u&&al[19].arg[0]==M0&&al[19].arg[1]==((M0&0xffff0000u)|5)&&al[19].eax==((M0&0xffff0000u)|5));
    CHECK(al[19].edx==M0&&al[19].ecx==M0&&al[19].context==0x20);
    CHECK(al[20].va==0x46e180u&&al[20].arg[0]==TERRAIN&&al[20].arg[1]==f-0x970&&al[20].eax==f-0x970&&al[20].edx==TERRAIN&&al[20].ecx==2000);
    CHECK(al[20].context==0x14&&r32(0,M0+0x26)==1000&&r32(0,M0+0x2a)==2000&&r32(0,M0+0x2e)==0x1298);
    CHECK(r32(0,M0+0xc)==0x80008000u&&r16(0,M0+0x10)==0&&r16(0,M0+0x32)==0);
    CHECK(al[21].va==0x4175dcu&&al[21].arg[0]==M0&&al[21].arg[1]==f-0x93a&&al[21].arg[2]==f-0x8fa&&al[21].eax==f-0x93a&&al[21].edx==M0+0xc&&al[21].ecx==f-0x8fa);
    CHECK(arena[M0+0x97-BASE]==1&&r32(0,M0+0x1b2)==TRIG&&r32(0,M0+0x1b6)==0);
    CHECK(al[22].va==0x411b90u&&al[22].arg[0]==M0&&al[22].arg[1]==f-0x922&&al[22].arg[2]==f-0x90e&&al[22].eax==f-0x90e&&al[22].edx==f-0x922&&al[22].ecx==0);
    CHECK(al[23].va==0x4178e8u&&al[23].arg[0]==M0&&al[23].arg[1]==0x55&&al[23].eax==0xb7&&al[23].edx==0xe7&&al[23].ecx==0x55&&r16(0,M0+0x252)==3);
    /* the MEC actor: its type 7, pool_alloc with EDX = the MEC record, the
     * player (4A9B84h, byte A+0A3h), the defaults, weapon_fit over the MEC
     * words, condition_init, the pilot word */
    CHECK(al[24].va==0x471b30u&&al[24].eax==MPOOL&&al[24].edx==MEC&&al[24].ecx==7);
    CHECK(al[25].va==0x415bb0u&&al[25].arg[1]==((M1&0xffff0000u)|7)&&al[25].edx==M1&&al[25].ecx==M1&&al[25].context==0x20);
    CHECK(wm_r32(0,WVA(0x4a9b84))==M1&&arena[M1+0xa3-BASE]==1);
    CHECK(r32(0,M1+0x26)==0&&r32(0,M1+0x2a)==0&&r32(0,M1+0x2e)==0xffff8000u&&r32(0,M1+0xc)==0x80008000u&&r16(0,M1+0x10)==0);
    CHECK(al[26].va==0x4175dcu&&al[26].arg[1]==MEC+8&&al[26].arg[2]==MEC+0x20&&al[26].eax==MEC+8&&al[26].edx==M1+0xc&&al[26].ecx==MEC+0x20);
    CHECK(al[27].va==0x415068u&&al[27].count==4&&al[27].arg[0]==M1+0x206&&al[27].arg[1]==MEC+0x3c&&al[27].arg[2]==MEC+0x56&&al[27].arg[3]==MEC+0x6a);
    CHECK(al[27].eax==MEC+0x3c&&al[27].edx==M1+0x206&&al[27].ecx==MEC+0x56&&r16(0,M1+0x29c)==0x0101&&r16(0,f-0x46)==1);
    /* the loop ends with EAX = N, ECX = M + N, EDX = i */
    CHECK(al[28].va==0x40fa88u&&al[28].eax==1&&al[28].edx==2&&al[28].ecx==2&&r32(0,COUNTER)==86);
    /* the flyer is read but not selected; the building: the factory with
     * the byte 9, the defaults, angle word 3333h, trigger 1 (58 bytes on),
     * action_copy */
    CHECK(al[29].va==0x4d0018u&&al[30].va==0x47af98u&&al[30].arg[1]==0x5c&&al[30].arg[2]==f-0x9cc);
    CHECK(al[31].va==0x4d0018u&&al[31].eax==VT&&al[31].edx==0xef&&al[31].ecx==f-0xc);
    CHECK(al[32].va==0x47af98u&&al[32].arg[1]==0x34&&al[32].ecx==0xe0);
    CHECK(al[33].va==0x405314u&&al[33].arg[0]==f-0xa0c&&al[33].arg[1]==9&&al[33].eax==9&&al[33].edx==f-0xa0c&&al[33].ecx==WVA(0x4aa9d2));
    CHECK(r32(0,B0+0x26)==0&&r32(0,B0+0x2a)==0&&r32(0,B0+0xc)==0&&r16(0,B0+0x10)==0x3333&&r16(0,B0+0x32)==0);   /* z: al[50] */
    CHECK(r32(0,B0+0x1b2)==TRIG+58&&r32(0,B0+0x1b6)==0);
    CHECK(al[34].va==0x411b90u&&al[34].arg[0]==B0&&al[34].arg[1]==f-0xa06&&al[34].arg[2]==f-0x9f2&&al[34].eax==f-0xa06&&al[34].edx==0&&al[34].ecx==f-0x9f2);
    /* the order: its count into 4A9E2Ch, new[](22), the runtime order and
     * subject_resolve(0, 1) */
    CHECK(al[35].va==0x4d0018u&&al[35].arg[2]==WVA(0x4a9e2c)&&al[35].eax==0xc3&&al[35].edx==f-0xc&&al[35].ecx==VT&&wm_r16(0,WVA(0x4a9e2c))==1);
    CHECK(al[36].va==0x4774f0u&&al[36].arg[0]==22&&al[36].eax==1&&al[36].edx==22&&al[36].ecx==0xe4&&wm_r32(0,WVA(0x4a9e30))==OB);
    CHECK(al[37].va==0x47af98u&&al[37].arg[1]==0xe&&al[37].eax==f-0xa28&&al[37].ecx==22);
    CHECK(r16(0,OB)==0x0a0a&&r16(0,OB+2)==0x0b0b&&r16(0,OB+0xc)==1&&r32(0,OB+0x12)==0&&r32(0,OB+8)==0&&r32(0,OB+4)==COORD);
    CHECK(al[38].va==0x425348u&&al[38].arg[0]==0&&al[38].arg[1]==1&&al[38].eax==1&&al[38].edx==COORD&&al[38].ecx==0&&r32(0,OB+0xe)==0xc7);
    /* the group: group 0's roster slot 0 = remap byte 1 (M + 0), the order
     * pointer, no heading (the first order has no route), group_ctor */
    CHECK(al[39].va==0x4d0018u&&al[39].eax==f-0x36&&al[40].va==0x47af98u&&al[40].arg[1]==0x9c&&al[40].eax==f-0xc&&al[40].edx==GA&&al[40].ecx==f-0xac4);
    CHECK(r16(0,f-0xa92)==1&&r16(0,f-0xa90)==0xffff&&r32(0,f-0xaec)==OB&&r32(0,f-0xae8)==0&&r16(0,f-0xce)==0);
    CHECK(al[41].va==0x423b34u&&al[41].count==6&&al[41].arg[0]==GA&&al[41].arg[1]==f-0xac4&&al[41].arg[2]==0&&al[41].arg[3]==f-0xaec);
    CHECK(al[41].arg[4]==0&&al[41].arg[5]==0&&al[41].eax==GA&&al[41].edx==0&&al[41].ecx==f-0xac4&&r16(0,f-0xc4)==0xffff);
    /* the objective: new[](76), the record, subject_resolve(2, 0), the
     * route pointer, the two 20-byte copies, three strings and 49A870h */
    CHECK(al[42].va==0x4d0018u&&al[42].arg[2]==WVA(0x4a9e3c)&&al[42].eax==0&&al[43].va==0x4774f0u&&al[43].arg[0]==76&&al[43].ecx==0xeb);
    CHECK(al[44].va==0x47af98u&&al[44].arg[1]==0x36&&al[44].ecx==76&&wm_r32(0,WVA(0x4a9e40))==OBJS);
    CHECK(al[45].va==0x425348u&&al[45].arg[0]==2&&al[45].arg[1]==0&&al[45].eax==2&&al[45].edx==0&&al[45].ecx==0);
    CHECK(r16(0,OBJS)==0x0c0c&&r16(0,OBJS+2)==0x0d0d&&r16(0,OBJS+4)==0&&r32(0,OBJS+6)==0xce&&r32(0,OBJS+0xa)==0&&r32(0,OBJS+0xe)==ROUTES);
    CHECK(al[46].va==0x49680eu&&al[46].arg[0]==OBJS+0x24&&al[46].arg[1]==f-0xb16&&al[46].arg[2]==0x14&&al[46].eax==f-0xb16&&al[46].edx==ROUTES&&al[46].ecx==OBJS+0x24);
    CHECK(al[47].va==0x49680eu&&al[47].arg[0]==OBJS+0x38&&al[47].eax==f-0xb02&&al[47].edx==OBJS+0x38&&al[47].ecx==0);
    CHECK(arena[OBJS+0x24-BASE]==0xe0&&arena[OBJS+0x37-BASE]==0xf3&&arena[OBJS+0x38-BASE]==0xf0&&arena[OBJS+0x4b-BASE]==0x03);
    CHECK(r32(0,OBJS+0x12)==0x57570001u&&r32(0,OBJS+0x16)==0x57570002u&&r32(0,OBJS+0x1a)==0x57570003u&&r32(0,OBJS+0x1e)==WVA(0x49a870));
    /* the terrain cell marks, the building at the ground height, the
     * trigger subjects (type 7: scope 1; type 3: cleared), the close */
    CHECK(al[48].va==0x47129cu&&al[48].arg[0]==TERRAIN&&al[48].eax==TERRAIN&&al[48].edx==WVA(0x4aa660)+16&&al[48].ecx==0x57570003u);
    CHECK(al[49].va==0x471c70u&&al[49].arg[0]==BPOOL&&al[49].arg[1]==0&&al[49].eax==BPOOL&&al[49].edx==0&&al[49].ecx==0);
    CHECK(al[50].va==0x46e180u&&al[50].arg[1]==f-0xb30&&al[50].eax==0xffff8000u&&al[50].edx==f-0xb30&&al[50].ecx==TERRAIN&&r32(0,B0+0x2e)==0x2222);
    CHECK(al[51].va==0x471c70u&&al[51].arg[1]==B0&&al[51].eax==BPOOL&&al[51].edx==0&&al[51].ecx==B0);
    CHECK(al[52].va==0x425348u&&al[52].arg[0]==3&&al[52].arg[1]==1&&al[52].eax==3&&al[52].edx==0x104&&al[52].ecx==0xf4);
    CHECK(r32(0,TRIG+0x36)==0xd5&&r32(0,TRIG+58+0x36)==0);
    CHECK(al[53].va==0x47ad78u&&al[53].count==2&&al[53].arg[0]==f-0xc&&al[53].arg[1]==2&&al[53].eax==5&&al[53].edx==f-0xc&&al[53].ecx==0xf5);
    CHECK(lo(p)==0xd6&&hi(p)==0x106&&ecx==0x5eadu);
}

/* The world load 424308 over a small script (two coordinates, one
 * angle, one route of two points, one region, one trigger, one timer, two
 * authored mechs, a flyer, two buildings, an order, three groups, an
 * objective, two string indices) with two MEC records (vehicle 8 first).
 * The logging call function numbers the calls k = 0, 1, ... and returns
 * EAX = A1h+k, EDX = D1h+k, ECX = C1h+k unless scripted: 49630c links the
 * record at f-54h (saved head 5EADh, context 0) and keeps EDX/ECX, memset
 * fills (EAX = dest, EDX = size, ECX = 0), 47ad40 stores the stream table
 * (slot +18h = 4D0018h), the reads (47af98 and the slot) copy the next
 * bytes of the stream's file (mission.var, player.mec or the script by the
 * stream address), new[] gives NEW0, NEW0+100h, ..., new the pool headers
 * and lists, 49667c the point and timer blocks, 4539a0 two string pointers
 * (AX = 2) and strlen the real length. Expected values are worked out by
 * hand from the contract in mission_load.md. */
#define WL_SCR (BASE+0x2e000u)
#define WL_NEW (BASE+0x20000u)
static uint8_t wl_files[3][2048];static unsigned wl_size[3],wl_pos[3],wl_new,wl_obj;
static wm_rec wl[136];static unsigned wl_count;static uint32_t wl_record,wl_objects[4],wl_blocks[2];
static void wl_put16(unsigned file,uint16_t w){wl_files[file][wl_size[file]++]=(uint8_t)w;wl_files[file][wl_size[file]++]=(uint8_t)(w>>8);}
static void wl_put32(unsigned file,uint32_t v){wl_put16(file,(uint16_t)v);wl_put16(file,(uint16_t)(v>>16));}
static uint8_t *wl_bytes(unsigned file,unsigned n,uint8_t fill){uint8_t *p=wl_files[file]+wl_size[file];memset(p,fill,n);wl_size[file]+=n;return p;}
static void wl_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    const uint32_t fr=WL_SCR+ES2_WORLD_LOAD_FRAME;
    (void)c;CHECK(wl_count<136);const unsigned k1=wl_count+1u;wm_rec *k=wl+wl_count++;memset(k,0,sizeof(*k));
    k->va=f-WM_IMAGE<sizeof(wm_image)?f-WM_IMAGE+0x400000u:f;for(unsigned i=0;i<n&&i<7;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(wl_record)k->context=wm_r16(0,wl_record+0x10u);
    if(k->va==0x49630cu){wl_record=fr-0x54u;wm_w32(0,wl_record,0x5eadu);wm_w16(0,wl_record+0x10u,0);r->eax=0x5eadu;return;}
    if(k->va==0x49681au){for(uint32_t i=0;i<a[2];++i)((uint8_t *)wm_host(0,a[0]+i))[0]=(uint8_t)a[1];
        r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    r->eax=0xa0u+k1;r->edx=0xd0u+k1;r->ecx=0xc0u+k1;
    switch(k->va){
    case 0x47ad40u:wm_w32(0,a[0],BASE+0x10000u);wm_w32(0,a[0]+4u,0);break;
    case 0x47af98u:case 0x4d0018u:{
        const unsigned file=a[0]==fr-0x18u?0u:a[0]==fr-0x24u?1u:2u;
        if(a[1]-1u<0x800u)for(uint32_t i=0;i<a[1];++i)
            ((uint8_t *)wm_host(0,a[2]+i))[0]=wl_pos[file]<wl_size[file]?wl_files[file][wl_pos[file]++]:0;
        break;}
    case 0x4774f0u:r->eax=WL_NEW+0x100u*wl_new++;break;
    case 0x47749cu:r->eax=wl_objects[wl_obj++];break;
    case 0x49667cu:r->eax=wl_blocks[a[1]==0x31u];break;
    case 0x4539a0u:wm_w32(0,a[1],BASE+0x11000u);wm_w32(0,a[1]+4u,BASE+0x11010u);r->eax=0x77770002u;break;
    case 0x496766u:{uint32_t len=0;while(((uint8_t *)wm_host(0,a[0]+len))[0])++len;r->eax=len;break;}
    }
}
static const es2_weapon_graph wl_graph={WM_IMAGE,WL_SCR,0,0,0,wl_call,0,fake_observe,0};
static void test_world_load(void){
    const uint32_t f=WL_SCR+ES2_WORLD_LOAD_FRAME,VT=BASE+0x10000u,S0=BASE+0x11000u,S1=BASE+0x11010u;
    const uint32_t COORDS=BASE+0x30000u,TIMERS=BASE+0x31000u,H1=BASE+0x32000u,L1=BASE+0x32100u,H2=BASE+0x32200u,L2=BASE+0x32300u;
    const uint32_t vars=f-0x18u,player=f-0x24u,stream=f-0xcu;
    uint32_t NEW[11];for(unsigned i=0;i<11;++i)NEW[i]=WL_NEW+0x100u*i;
    const uint32_t MEC=NEW[0],T=NEW[5];
    uint32_t ecx=0;
    wm_setup();wl_count=0;wl_record=0;wl_new=0;wl_obj=0;memset(wl_size,0,sizeof(wl_size));memset(wl_pos,0,sizeof(wl_pos));
    wl_objects[0]=H1;wl_objects[1]=L1;wl_objects[2]=H2;wl_objects[3]=L2;wl_blocks[0]=COORDS;wl_blocks[1]=TIMERS;
    w32(0,VT+0x18,0x4d0018u);
    memcpy(arena+(S0-BASE),"AB\n",4);memcpy(arena+(S1-BASE),"C",2);
    wm_w32(0,WVA(0x4a09f0),0x9f09f0u);
    /* stale selection and remap bytes (cleared by the memsets) */
    memset(wm_image+0xaa72a,0x77,0x334);
    /* mission.var: 2000 bytes of 5Ah */
    wl_bytes(0,2000,0x5a);
    /* player.mec: player ordinal 1, two records: (0101h, 0202h, vehicle 8,
     * two items), (0303h, 0404h, vehicle 7, one item) */
    wl_put16(1,1);wl_put16(1,2);
    wl_put16(1,0x0101);wl_put16(1,0x0202);wl_put16(1,8);wl_put16(1,2);wl_put16(1,0x11);wl_put16(1,0x12);wl_put16(1,0x15);wl_put16(1,0x16);
    wl_put16(1,0x3a3a);wl_bytes(1,26,0x26);wl_bytes(1,20,0x20);wl_bytes(1,20,0x21);
    wl_put16(1,0x0303);wl_put16(1,0x0404);wl_put16(1,7);wl_put16(1,1);wl_put16(1,0x31);wl_put16(1,0x35);
    wl_put16(1,0x3b3b);wl_bytes(1,26,0x36);wl_bytes(1,20,0x30);wl_bytes(1,20,0x31);
    /* the script: header words 2 (world), 31h (zone), 77h (cleared), 1234h,
     * 44h (word 4), 5 .. 9 */
    {static const uint16_t header[10]={2,0x31,0x77,0x1234,0x44,5,6,7,8,9};for(unsigned i=0;i<10;++i)wl_put16(2,header[i]);}
    wl_put16(2,2);wl_put32(2,100);wl_put32(2,0xffffff38u);wl_put32(2,7);wl_put32(2,50);wl_put32(2,300);wl_put32(2,9);
    wl_put16(2,1);wl_put16(2,3);
    wl_put16(2,1);wl_put16(2,2);wl_put16(2,1);wl_put16(2,0);
    wl_put16(2,1);wl_put16(2,0);wl_put16(2,0);wl_put16(2,2);
    /* one trigger: type 7, word 22h, region indices (0, -1, 5 ...), the
     * blocks A1h/A2h/A3h, word +34h = 4, the word -3 */
    wl_put16(2,1);wl_put16(2,7);wl_put16(2,0x22);wl_put16(2,0);wl_put16(2,0xffff);for(unsigned i=0;i<6;++i)wl_put16(2,5);
    wl_bytes(2,20,0xa1);wl_bytes(2,20,0xa2);wl_bytes(2,10,0xa3);wl_put16(2,4);wl_put16(2,0xfffd);
    /* one timer: first trigger 0, duration 500, triggers (0, -1 x 9) */
    wl_put16(2,1);wl_put16(2,0);wl_put16(2,500);wl_put16(2,0);for(unsigned i=0;i<9;++i)wl_put16(2,0xffff);
    /* mechs (types 0Ah, 0Bh), a flyer (0Ch), buildings (0Dh, 0Eh), an order */
    wl_put16(2,2);wl_bytes(2,134,0)[0x28]=0x0a;wl_bytes(2,134,0)[0x28]=0x0b;
    wl_put16(2,1);wl_bytes(2,92,0)[0x2c]=0x0c;
    wl_put16(2,2);wl_bytes(2,52,0)[0]=0x0d;wl_bytes(2,52,0)[0]=0x0e;
    wl_put16(2,1);wl_bytes(2,14,0x0e);
    /* groups: group 0 with the roster (2, 3), group 1 class 0 member 1,
     * group 2 class 2 member 0 */
    wl_put16(2,3);
    {uint8_t *G=wl_bytes(2,156,0xff);G[0x32]=2;G[0x33]=0;G[0x34]=3;G[0x35]=0;}
    {uint8_t *G=wl_bytes(2,156,0xff);G[0x28]=G[0x29]=0;G[0x32]=1;G[0x33]=0;}
    {uint8_t *G=wl_bytes(2,156,0xff);G[0x28]=2;G[0x29]=0;G[0x32]=G[0x33]=0;}
    wl_put16(2,1);wl_bytes(2,54,0x54);
    wl_put16(2,2);wl_put16(2,5);wl_put16(2,6);
    uint64_t p=es2_world_load_registers(&wm_memory,&wl_graph,entry,&ecx);
    static const uint32_t vas[125]={0x49630c,0x49681a,0x49681a,0x49681a,0x49681a,0x49681a,0x49681a,0x49681a,0x49681a,0x49681a,0x49681a,
        0x45ee00,0x47ad40,0x47af98,0x47ad78,0x45ee00,0x47ad40,0x4d0018,0x4d0018,0x4774f0,
        0x4d0018,0x4d0018,0x4d0018,0x4d0018,0x47af98,0x47af98,0x4d0018,0x47af98,0x47af98,0x47af98,
        0x4d0018,0x4d0018,0x4d0018,0x4d0018,0x47af98,0x47af98,0x4d0018,0x47af98,0x47af98,0x47af98,
        0x47ad78,0x45ee00,0x453878,0x4539a0,0x496766,0x496766,0x45ee00,0x492b90,0x47ad40,0x47af98,0x42e010,
        0x4d0018,0x49667c,0x47af98,0x47af98,0x4d0018,0x4774f0,0x47af98,0x4d0018,0x4774f0,0x4d0018,0x4774f0,0x4d0018,0x4d0018,
        0x4d0018,0x4774f0,0x47af98,0x423358,0x4d0018,0x4774f0,0x4d0018,0x4d0018,0x47af98,0x47af98,0x47af98,0x47af98,0x4d0018,0x4d0018,
        0x4774f0,0x4d0018,0x49667c,0x47af98,0x423104,0x4d0018,0x47af98,0x47af98,0x4d0018,0x47af98,0x4d0018,0x47af98,0x47af98,
        0x4d0018,0x47af98,0x4d0018,0x423d38,0x47af98,0x47af98,0x47af98,0x4d0018,0x47af98,0x4d0018,0x4774f0,0x4d0018,
        0x41ff10,0x41ff10,0x41ff10,0x405ebc,0x47749c,0x471ad8,0x47749c,0x4774f0,0x47749c,0x471ad8,0x47749c,0x4774f0,
        0x405df4,0x411df8,0x40fecc,0x40d218,0x40cbec,0x42049c,0x422fb4,0x42789c,0x4774f0,0x47ad78};
    CHECK(wl_count==125&&observed==125&&slot_seen==33);
    for(unsigned i=0;i<125;++i)CHECK(wl[i].va==vas[i]);
    /* the record, the memsets (EDX/ECX carried, the two frame arrays in
     * EDX and ECX) */
    CHECK(wl[0].eax==WVA(0x49a7fc)&&wl[0].edx==0xe1&&wl[0].ecx==0xe2);
    CHECK(wl[1].count==3&&wl[1].arg[0]==WVA(0x4aa72a)&&wl[1].arg[1]==0&&wl[1].arg[2]==100&&wl[1].eax==0x5ead&&wl[1].edx==0xe1&&wl[1].ecx==0xe2);
    CHECK(wl[3].arg[0]==f-0x9e4&&wl[3].arg[2]==200&&wl[3].eax==WVA(0x4aa78e)&&wl[3].edx==f-0x9e4&&wl[3].ecx==0);
    CHECK(wl[7].arg[0]==f-0xa48&&wl[7].arg[2]==100&&wl[7].eax==WVA(0x4aa888)&&wl[7].edx==50&&wl[7].ecx==f-0xa48);
    CHECK(wl[10].arg[0]==WVA(0x4aa9d2)&&wl[10].arg[2]==140);
    /* mission.var */
    CHECK(wl[11].count==1&&wl[11].arg[0]==WVA(0x49a9cb)&&wl[11].eax==WVA(0x4aa9d2)&&wl[11].edx==140&&wl[11].ecx==0&&wl[11].context==8);
    CHECK(wl[12].count==2&&wl[12].arg[0]==vars&&wl[12].arg[1]==0xac&&wl[12].eax==vars&&wl[12].edx==0xdc&&wl[12].ecx==WVA(0x49a9cb));
    CHECK(wl[13].arg[0]==vars&&wl[13].arg[1]==2000&&wl[13].arg[2]==WVA(0x4a9e70)&&wl[13].eax==0xad&&wl[13].edx==0xdd&&wl[13].ecx==vars);
    CHECK(wl[14].arg[0]==vars&&wl[14].arg[1]==2&&wl[14].eax==vars&&wl[14].edx==0xde&&wl[14].ecx==0xce);
    for(unsigned i=0;i<1000;++i){const uint16_t w=wm_r16(0,WVA(0x4a9e70)+2*i);CHECK(w==((i==10||(i>=20&&i<=42))?0:0x5a5a));}
    /* player.mec */
    CHECK(wl[15].arg[0]==WVA(0x49a9f2)&&wl[15].context==0x14&&wl[16].arg[0]==player&&wl[16].arg[1]==0xb0&&wl[16].ecx==WVA(0x49a9f2));
    CHECK(wl[17].arg[0]==player&&wl[17].arg[1]==2&&wl[17].arg[2]==WVA(0x4aaa64)&&wl[17].eax==0xb1&&wl[17].edx==0xe1&&wl[17].ecx==VT);
    CHECK(wl[18].arg[2]==WVA(0x4aaa5e)&&wl[18].eax==VT&&wl[18].edx==0xe2&&wl[18].ecx==0xd2);
    CHECK(wl[19].arg[0]==252&&wl[19].eax==0xb3&&wl[19].edx==2&&wl[19].ecx==252);
    CHECK(wm_r16(0,WVA(0x4aaa64))==1&&wm_r16(0,WVA(0x4aaa5e))==2&&wm_r32(0,WVA(0x4aaa60))==MEC);
    CHECK(wl[20].arg[0]==player&&wl[20].arg[2]==MEC&&wl[20].eax==MEC&&wl[20].edx==VT&&wl[20].ecx==252);
    CHECK(wl[23].arg[2]==MEC+6&&wl[23].eax==MEC+6&&wl[23].ecx==0xd7);
    CHECK(wl[24].arg[1]==4&&wl[24].arg[2]==MEC+8&&wl[24].eax==4&&wl[24].edx==0xe8&&wl[24].ecx==MEC+8);
    CHECK(wl[25].arg[1]==4&&wl[25].arg[2]==MEC+0x20&&wl[25].eax==0xb9&&wl[25].edx==MEC+0x20&&wl[25].ecx==4);
    CHECK(wl[26].arg[2]==MEC+0x3a&&wl[26].eax==MEC+0x3a&&wl[26].edx==VT&&wl[26].ecx==0xda);
    CHECK(wl[27].arg[1]==26&&wl[27].arg[2]==MEC+0x3c&&wl[27].eax==0xbb&&wl[27].edx==0xeb&&wl[27].ecx==MEC+0x3c);
    CHECK(wl[28].arg[1]==20&&wl[28].arg[2]==MEC+0x56&&wl[28].eax==MEC+0x56&&wl[29].arg[2]==MEC+0x6a&&wl[29].edx==MEC+0x6a);
    CHECK(wl[30].arg[2]==MEC+0x7e&&wl[34].arg[1]==2&&wl[34].arg[2]==MEC+0x86&&wl[35].arg[2]==MEC+0x9e&&wl[39].arg[2]==MEC+0xe8);
    CHECK(r16(0,MEC)==0x0101&&r16(0,MEC+4)==8&&r16(0,MEC+6)==2&&r16(0,MEC+8)==0x11&&r16(0,MEC+0xa)==0x12&&r16(0,MEC+0x20)==0x15);
    CHECK(r16(0,MEC+0x3a)==0x3a3a&&arena[MEC+0x3c-BASE]==0x26&&arena[MEC+0x56-BASE]==0x20&&arena[MEC+0x7d-BASE]==0x21);
    CHECK(r16(0,MEC+4+0x7e)==7&&r16(0,MEC+8+0x7e)==0x31&&r16(0,MEC+0x20+0x7e)==0x35&&r16(0,MEC+0x3a+0x7e)==0x3b3b);
    CHECK(wl[40].va==0x47ad78u&&wl[40].arg[0]==player&&wl[40].eax==0xc8&&wl[40].edx==0xf8&&wl[40].ecx==0xe8);
    /* the vehicle-8 byte, mission.str, the LF removed */
    CHECK(wm_image[0xd2571]==1&&r32(0,f-0x6c)==MEC);
    CHECK(wl[41].arg[0]==WVA(0x49aa17)&&wl[41].eax==MEC&&wl[41].edx==1&&wl[41].ecx==MEC);
    CHECK(wl[42].count==2&&wl[42].arg[0]==WVA(0x4aa650)&&wl[42].arg[1]==0xca&&wl[42].eax==0xca&&wl[42].ecx==WVA(0x49aa17));
    CHECK(wl[43].count==4&&wl[43].arg[0]==WVA(0x4aa650)&&wl[43].arg[1]==WVA(0x4aa660)&&wl[43].arg[2]==50&&wl[43].arg[3]==0);
    CHECK(wl[44].arg[0]==S0&&wl[44].eax==S0&&wl[44].edx==0xfc&&wl[44].ecx==2&&r32(0,f-0x70)==2);
    CHECK(wl[45].arg[0]==S1&&wl[45].eax==S1&&wl[45].edx==S0&&wl[45].ecx==0x0a);
    CHECK(memcmp(arena+(S0-BASE),"AB\0",3)==0&&memcmp(arena+(S1-BASE),"C",2)==0);
    /* the script: name, stream, header, the world */
    CHECK(wl[46].arg[0]==WVA(0x49aa28)&&wl[46].eax==0x9f09f0u&&wl[46].edx==S1&&wl[46].ecx==0x43);
    CHECK(wl[47].count==2&&wl[47].arg[0]==0xcf&&wl[47].arg[1]==0x9f09f0u&&wl[47].eax==0xcf&&wl[47].edx==0xff&&wl[47].ecx==WVA(0x49aa28));
    CHECK(wl[48].arg[0]==stream&&wl[48].arg[1]==0xd0&&wl[48].eax==stream&&wl[48].edx==0x100&&wl[48].ecx==0xd0&&wl[48].context==0x20);
    CHECK(wl[49].arg[1]==20&&wl[49].arg[2]==WVA(0x4a9e4e)&&wl[49].eax==stream&&wl[49].edx==0x101&&wl[49].ecx==0xf1&&r32(0,f-0x74)==0xd0);
    CHECK(wl[50].count==1&&wl[50].arg[0]==WVA(0x4a9e4e)&&wl[50].eax==0xd2&&wl[50].edx==0x102&&wl[50].ecx==0xf2);
    CHECK(wm_r16(0,WVA(0x4a9e4e))==2&&wm_r16(0,WVA(0x4a9e50))==0x31&&wm_r16(0,WVA(0x4a9e52))==0&&wm_r16(0,WVA(0x4a9e54))==0x1234);
    /* coordinates and the rectangle */
    CHECK(wl[51].arg[2]==WVA(0x4a9e06)&&wl[51].eax==0xd3&&wl[51].edx==stream&&wl[51].ecx==VT);
    CHECK(wl[52].count==5&&wl[52].arg[0]==0&&wl[52].arg[1]==12&&wl[52].arg[2]==2&&wl[52].arg[3]==1&&wl[52].arg[4]==WVA(0x4953c4));
    CHECK(wl[52].eax==2&&wl[52].edx==0x104&&wl[52].ecx==0xf4&&wm_r32(0,WVA(0x4a9e0c))==COORDS);
    CHECK(wl[53].arg[1]==12&&wl[53].arg[2]==f-0xb6c&&wl[53].eax==f-0xb6c&&wl[53].edx==stream&&wl[53].ecx==0xf5);
    CHECK(wl[54].ecx==0xffffff38u&&r32(0,COORDS)==100&&r32(0,COORDS+4)==0xffffff38u&&r32(0,COORDS+8)==7&&r32(0,COORDS+12)==50);
    CHECK(wm_r32(0,WVA(0x4aa640))==50&&wm_r32(0,WVA(0x4aa644))==0xffffff38u&&wm_r32(0,WVA(0x4aa648))==100&&wm_r32(0,WVA(0x4aa64c))==300);
    /* angles x 182, the route's two point pointers */
    CHECK(wl[55].arg[2]==WVA(0x4a9e08)&&wl[55].eax==300&&wl[55].edx==stream&&wl[55].ecx==VT);
    CHECK(wl[56].arg[0]==2&&wl[56].eax==2&&wl[56].edx==0x108&&wl[56].ecx==0xf8&&wm_r32(0,WVA(0x4a9e10))==NEW[1]&&r16(0,NEW[1])==546);
    CHECK(wl[57].arg[2]==f-0xfe&&wl[57].ecx==2&&wl[58].eax==VT&&wl[58].edx==NEW[1]&&wl[58].ecx==stream);
    CHECK(wl[59].arg[0]==6&&wl[59].eax==0xdb&&wl[59].edx==1&&wl[59].ecx==6&&wm_r32(0,WVA(0x4a9e18))==NEW[2]);
    CHECK(wl[60].arg[2]==NEW[2]&&wl[60].eax==NEW[2]&&wl[60].edx==stream&&wl[60].ecx==VT);
    CHECK(wl[61].arg[0]==8&&wl[61].eax==8&&wl[61].edx==0x10d&&wl[61].ecx==0xfd&&r32(0,NEW[2]+2)==NEW[3]);
    CHECK(wl[62].arg[2]==f-0x108&&wl[62].eax==f-0x108&&wl[62].edx==stream&&wl[62].ecx==VT&&r16(0,f-0x106)==2);
    CHECK(r32(0,NEW[3])==COORDS+12&&r32(0,NEW[3]+4)==COORDS);
    /* the region: region_init with four slots, CX = the coordinate count */
    CHECK(wl[64].arg[2]==WVA(0x4a9e1c)&&wl[64].eax==stream&&wl[64].edx==VT&&wl[64].ecx==2);
    CHECK(wl[65].arg[0]==10&&wl[65].eax==10&&wl[65].ecx==1&&wl[66].arg[1]==6&&wl[66].ecx==10);
    CHECK(wl[67].count==4&&wl[67].arg[0]==NEW[4]&&wl[67].arg[1]==f-0x114&&wl[67].arg[2]==COORDS&&wl[67].arg[3]==2);
    CHECK(wl[67].eax==COORDS&&wl[67].edx==f-0x114&&wl[67].ecx==NEW[4]&&r32(0,f-0x10c)==NEW[4]+10);
    /* the trigger */
    CHECK(wl[68].eax==stream&&wl[68].edx==VT&&wl[68].ecx==0x104&&wl[69].arg[0]==58&&wl[69].ecx==1);
    CHECK(wl[70].arg[2]==T&&wl[70].eax==T&&wl[70].edx==stream&&wl[70].ecx==VT);
    CHECK(wl[71].arg[2]==T+2&&wl[71].eax==VT&&wl[71].edx==T+2&&wl[71].ecx==stream);
    CHECK(wl[72].arg[1]==16&&wl[72].arg[2]==f-0xb7c&&wl[72].eax==0xe8&&wl[72].edx==f-0xb7c&&wl[72].ecx==stream);
    CHECK(wl[73].arg[2]==T+0xc&&wl[73].eax==T+0xc&&wl[74].arg[2]==T+0x20&&wl[74].eax==stream&&wl[74].ecx==T+0x20);
    CHECK(wl[75].arg[1]==10&&wl[75].arg[2]==f-0x120&&wl[76].arg[2]==T+0x34&&wl[76].eax==VT&&wl[76].edx==T+0x34);
    CHECK(wl[77].arg[2]==f-0x122&&wl[77].edx==f-0x122&&wl[77].ecx==stream);
    CHECK(wl[78].arg[0]==4&&wl[78].eax==f-0xb7a&&wl[78].edx==0xfffffffdu&&wl[78].ecx==0x10e);
    CHECK(r16(0,T)==7&&r16(0,T+2)==0x22&&r16(0,T+4)==1&&r32(0,T+6)==NEW[6]&&r16(0,T+0xa)==0&&arena[T+0xc-BASE]==0xa1);
    CHECK(arena[T+0x1f-BASE]==0xa1&&arena[T+0x20-BASE]==0xa2&&arena[T+0x33-BASE]==0xa2&&r16(0,T+0x34)==3&&r32(0,T+0x36)==0xfffffffdu);
    CHECK(r32(0,NEW[6])==NEW[4]);
    /* the timer */
    CHECK(wl[79].arg[2]==WVA(0x4a9e34)&&wl[79].eax==VT&&wl[79].edx==1&&wl[79].ecx==NEW[4]);
    CHECK(wl[80].count==5&&wl[80].arg[1]==0x31&&wl[80].arg[2]==1&&wl[80].arg[4]==WVA(0x427207)&&wl[80].eax==0xf0&&wl[80].edx==1&&wl[80].ecx==0x110);
    CHECK(wl[81].arg[1]==24&&wl[81].arg[2]==f-0xb94&&wl[81].ecx==0x111&&wm_r32(0,WVA(0x4a9e38))==TIMERS);
    CHECK(wl[82].count==4&&wl[82].arg[0]==TIMERS&&wl[82].arg[1]==T&&wl[82].arg[2]==f-0xbbc&&wl[82].arg[3]==500);
    CHECK(wl[82].eax==T&&wl[82].edx==0&&wl[82].ecx==TIMERS&&r32(0,f-0x134)==T&&r32(0,f-0xbbc)==T&&r32(0,f-0xbb8)==0&&r32(0,f-0xb98)==0);
    /* the actor lists, the groups and their selection */
    CHECK(wl[83].arg[2]==f-0x56&&wl[83].eax==f-0x56&&wl[83].edx==stream&&wl[83].ecx==VT);
    CHECK(wl[84].arg[1]==134&&wl[84].ecx==0x114&&wl[85].ecx==0x0a&&wl[87].arg[1]==92&&wl[89].arg[1]==52&&wl[90].ecx==0x0d);
    CHECK(wl[92].arg[1]==14&&wl[92].arg[2]==f-0xce4&&wl[93].arg[2]==f-0x62&&wl[93].eax==stream&&wl[93].edx==VT&&wl[93].ecx==f-0x62);
    CHECK(wl[94].count==2&&wl[94].arg[0]==WVA(0x4a9e00)&&wl[94].arg[1]==3&&wl[94].eax==0xfe&&wl[94].edx==0x12e&&wl[94].ecx==3);
    CHECK(wl[95].arg[2]==f-0xd80&&wl[96].arg[2]==f-0xe1c&&wl[96].eax==stream&&wl[96].ecx==f-0xe1c&&wl[97].edx==20);
    CHECK(wl[98].arg[2]==f-0x66&&wl[98].eax==VT&&wl[98].edx==stream&&wl[98].ecx==1&&wl[99].arg[1]==54);
    CHECK(wl[100].arg[2]==WVA(0x4a9e44)&&wl[100].eax==VT&&wl[100].ecx==stream);
    CHECK(wl[101].arg[0]==4&&wl[101].eax==0x105&&wl[101].edx==4&&wl[101].ecx==0x125);
    CHECK(wl[102].arg[1]==4&&wl[102].arg[2]==NEW[7]&&wl[102].eax==stream&&wl[102].edx==VT&&wl[102].ecx==4&&r16(0,NEW[7]+2)==6);
    CHECK(r16(0,f-0xdae)==1&&r32(0,f-0x138)==NEW[7]&&r32(0,f-0x13c)==2);
    /* selection, compaction and the MEC actors (mech flags 0,1,1,1; remap
     * 1 -> 0, the MEC actors 2, 3 -> 1, 2; building 0 with factory byte 1) */
    {static const uint8_t mf[5]={0,1,1,1,0},mr[5]={0,0,1,2,0},mec[5]={0,0,1,1,0};
     for(unsigned i=0;i<5;++i)CHECK(wm_image[i+0xaa72a]==mf[i]&&wm_image[i+0xaa78e]==mr[i]&&wm_image[i+0xaa7f2]==mec[i]);}
    CHECK(wm_image[0xaa72a+99]==0&&wm_image[0xaa856]==0&&wm_image[0xaa888]==0&&wm_image[0xaa8ba]==1&&wm_image[0xaa8bb]==0);
    CHECK(wm_image[0xaa946]==0&&wm_image[0xaa9d2]==1&&wm_image[0xaa9d3]==0);
    CHECK(r16(0,f-0x9e4)==0x0a&&r16(0,f-0x9e2)==0x0b&&r16(0,f-0x9e0)==8&&r16(0,f-0x9de)==7&&r16(0,f-0xa48)==0x0c);
    CHECK(r16(0,f-0xb60)==0x0d&&r16(0,f-0xb5e)==0x0e&&r16(0,f-0x56)==4&&r16(0,f-0x5c)==3&&r16(0,f-0x5e)==0&&r16(0,f-0x60)==1);
    /* the type selects (EDX = the type word over the last EDX; ECX = the
     * last MEC actor's remap byte over its type word) */
    CHECK(wl[103].count==1&&wl[103].arg[0]==0x0b&&wl[103].eax==WVA(0x4aa72b)&&wl[103].edx==0x0b&&wl[103].ecx==2);
    CHECK(wl[104].arg[0]==8&&wl[104].eax==WVA(0x4aa72c)&&wl[104].edx==8&&wl[104].ecx==0x0b);
    CHECK(wl[105].arg[0]==7&&wl[105].eax==WVA(0x4aa72d)&&wl[105].edx==7&&wl[105].ecx==8);
    CHECK(wl[106].count==2&&wl[106].arg[0]==((WVA(0x4aa8ba)&0xffff0000u)|0x0d)&&wl[106].arg[1]==1);
    CHECK(wl[106].eax==((WVA(0x4aa8ba)&0xffff0000u)|0x0d)&&wl[106].edx==WVA(0x4aa9d2)&&wl[106].ecx==1);
    /* the pools and lists */
    CHECK(wl[107].arg[0]==0x12&&wl[107].eax==WVA(0x4aa8bb)&&wl[107].edx==3&&wl[107].ecx==0x12b&&r16(0,f-0x13e)==3);
    CHECK(wl[108].count==3&&wl[108].arg[0]==H1&&wl[108].arg[1]==3&&wl[108].arg[2]==0x36a&&wl[108].eax==H1&&wl[108].edx==3&&wl[108].ecx==H1&&wl[108].context==0x38);
    CHECK(wl[109].arg[0]==8&&wl[109].eax==H1&&wl[109].context==0x2c&&wm_r32(0,WVA(0x4a9b7a))==H1);
    CHECK(wl[110].arg[0]==100&&wl[110].eax==100&&wl[110].edx==L1&&wl[110].ecx==0x19&&wl[110].context==0x44);
    CHECK(r32(0,L1)==NEW[8]&&r16(0,L1+4)==0&&r16(0,L1+6)==25&&wm_r32(0,WVA(0x4a9b7e))==L1&&r16(0,f-0x140)==0);
    CHECK(wl[111].arg[0]==0x12&&wl[111].eax==(L1&0xffff0000u)&&wl[111].edx==L1&&wl[111].ecx==100&&wl[111].context==0x2c);
    CHECK(wl[112].arg[0]==H2&&wl[112].arg[1]==0&&wl[112].arg[2]==0x291&&wl[112].eax==H2&&wl[112].edx==0&&wl[112].context==0x5c);
    CHECK(wl[113].context==0x50&&wm_r32(0,WVA(0x4a9db9))==H2&&wl[114].edx==L2&&wl[114].context==0x68);
    CHECK(r32(0,L2)==NEW[9]&&r16(0,L2+6)==25&&wm_r32(0,WVA(0x4a9dbd))==L2);
    CHECK(wl[115].count==1&&wl[115].arg[0]==((L2&0xffff0000u)|1)&&wl[115].eax==((L2&0xffff0000u)|1)&&wl[115].edx==L2&&wl[115].ecx==100&&wl[115].context==0x50);
    CHECK(wl[116].count==2&&wl[116].arg[0]==WVA(0x4a9af8)&&wl[116].arg[1]==4&&wl[116].eax==4&&wl[116].edx==0x144&&wl[116].ecx==((L2&0xffff0000u)|4));
    CHECK(r16(0,f-0x142)==4&&wl[117].count==0&&wl[117].eax==0x115&&wl[117].edx==0x145&&wl[117].ecx==0x135&&wl[121].ecx==0x139);
    /* the terrain, the result pairs, word 4, the close */
    CHECK(wl[122].count==2&&wl[122].arg[0]==0x31&&wl[122].arg[1]==2&&wl[122].eax==0x11a&&wl[122].edx==2&&wl[122].ecx==0x31);
    CHECK(wl[123].arg[0]==200&&wl[123].eax==0x11b&&wl[123].edx==0x14b&&wl[123].ecx==0x13b);
    CHECK(wm_r16(0,WVA(0x4a9e6a))==0&&wm_r32(0,WVA(0x4a9e6c))==NEW[10]&&wm_r16(0,WVA(0x4aa728))==0x44);
    CHECK(wl[124].count==2&&wl[124].arg[0]==stream&&wl[124].arg[1]==2&&wl[124].eax==((NEW[10]&0xffff0000u)|0x44)&&wl[124].edx==stream&&wl[124].ecx==200);
    CHECK(lo(p)==0x11d&&hi(p)==0x14d&&ecx==0x5eadu);
}
/* actor_list_init 411df8 and the three slot tables. */
static void test_world_leaves(void){
    uint32_t ecx=0;const uint32_t LIST=BASE+0x40000u;
    wm_setup();wl_count=0;wl_record=0;wl_new=0;
    uint64_t p=es2_actor_list_init_registers(&wm_memory,&wl_graph,LIST,0x7777fffeu,entry,&ecx);
    CHECK(wl_count==1&&wl[0].va==0x4774f0u&&wl[0].count==1&&wl[0].arg[0]==0xfffffff8u&&wl[0].eax==0xfffffff8u&&wl[0].edx==0xe1&&wl[0].ecx==0xe2);
    CHECK(r32(0,LIST)==WL_NEW&&r16(0,LIST+4)==0xfffe&&lo(p)==WL_NEW&&hi(p)==0xd1&&ecx==0xfffffff8u);
    /* gun table: always new[] (also for 0) */
    wm_setup();wl_count=0;wl_new=0;wm_w16(0,WVA(0x4a98e8),0);
    p=es2_gun_slot_table_registers(&wm_memory,&wl_graph,entry,&ecx);
    CHECK(wl_count==1&&wl[0].arg[0]==0&&wl[0].eax==0&&wl[0].edx==0&&wl[0].ecx==0xe2&&wm_r32(0,WVA(0x4a98e4))==WL_NEW);
    CHECK(lo(p)==WL_NEW&&hi(p)==0xd1&&ecx==0);
    wm_setup();wl_count=0;wl_new=0;wm_w16(0,WVA(0x4a98e8),7);
    p=es2_gun_slot_table_registers(&wm_memory,&wl_graph,entry,&ecx);
    CHECK(wl_count==1&&wl[0].arg[0]==42&&wl[0].eax==7&&wl[0].edx==42&&ecx==42);
    /* damage and collision tables: nothing for 0 (the entry registers) */
    wm_setup();wl_count=0;wl_new=0;wm_w16(0,WVA(0x498856),0);wm_w32(0,WVA(0x4a9828),0x2828u);
    p=es2_dmg_slot_table_registers(&wm_memory,&wl_graph,entry,&ecx);
    CHECK(wl_count==0&&wm_r32(0,WVA(0x4a9828))==0x2828u&&lo(p)==0xe0&&hi(p)==0xe1&&ecx==0xe2);
    wm_w16(0,WVA(0x498856),0xfffd);
    p=es2_dmg_slot_table_registers(&wm_memory,&wl_graph,entry,&ecx);
    CHECK(wl_count==1&&wl[0].arg[0]==0xffffffdcu&&wl[0].eax==0xfffffffdu&&wl[0].edx==0xffffffdcu&&wl[0].ecx==0xe2);
    CHECK(wm_r32(0,WVA(0x4a9828))==WL_NEW&&lo(p)==WL_NEW&&hi(p)==0xd1&&ecx==0xffffffdcu);
    wm_setup();wl_count=0;wl_new=0;wm_w16(0,WVA(0x4987dc),0);
    p=es2_col_slot_table_registers(&wm_memory,&wl_graph,entry,&ecx);
    CHECK(wl_count==0&&lo(p)==0xe0&&hi(p)==0xe1&&ecx==0xe2);
    wm_w16(0,WVA(0x4987dc),5);
    p=es2_col_slot_table_registers(&wm_memory,&wl_graph,entry,&ecx);
    CHECK(wl_count==1&&wl[0].arg[0]==30&&wl[0].eax==5&&wl[0].edx==30&&wm_r32(0,WVA(0x4a9824))==WL_NEW&&ecx==30);
}

int main(void){
    test_pool_create();test_pool_nth();test_subject_resolve();test_action_copy();test_condition_init();
    test_region_timer_group();test_initial_damage();
    test_light_pools();test_smoke_projectile_pools();
    test_player_weapons();test_weapon_factory();test_weapon_fit();
    test_mech_ctor();test_flyer_ctor();test_building_factory();test_group_ctor();test_base_marks();
    test_actor_load();test_world_load();test_world_leaves();
    printf("mission_load: %u checks passed\n",checks);
    return 0;
}
