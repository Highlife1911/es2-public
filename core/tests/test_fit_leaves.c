#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/fit_leaves.h"
/* Synthetic state for the core (the remaining callees of the weapon
 * fit). The image base is placed so that the globals 4a98e4 and 4a9d70 fall
 * inside the arena. A logging call function stands in for every game call
 * (target, arguments, count, entry EAX/EDX/ECX): the memset thunk 49681a
 * clears its buffer, the shape node's virtual slot 0 (an address METHOD +
 * class id) returns the class id, and every other call returns the scripted
 * registers. Expected values are worked out by hand from the contract in
 * mission_load.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define METHOD UINT32_C(0x77000000)
static uint8_t arena[0x20000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"fit_leaves line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct call { uint32_t f,arg[3],count,eax,edx,ecx; } call;
static call calls[64];static unsigned call_count,observed,scripted;
static uint32_t script_eax[8],script_edx[8],script_ecx[8];
static uint32_t va_of(uint32_t f){return f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<64);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->f=va_of(f);for(unsigned i=0;i<n&&i<3;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(f>=METHOD&&f-METHOD<0x400000u){r->eax=f-METHOD;r->edx=0x5000u+call_count;r->ecx=0x6000u+call_count;return;}
    if(k->f==0x49681au){CHECK(a[1]==0&&a[2]==0x14u);memset(host(0,a[0]),0,a[2]);}
    if(scripted<8){r->eax=script_eax[scripted];r->edx=script_edx[scripted];r->ecx=script_ecx[scripted];++scripted;}
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;++observed;}
static es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0e0e0e0u,0xe1e1e1e1u,0xabcd1234u};
static void setup(void){memset(arena,0xcc,sizeof(arena));call_count=observed=scripted=0;}
static void script(unsigned i,uint32_t eax,uint32_t edx,uint32_t ecx){script_eax[i]=eax;script_edx[i]=edx;script_ecx[i]=ecx;}
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}

/* weapon_shape_word: AX = the first word of [W+18h]; EAX keeps the pointer's
 * high word; EDX and ECX unchanged. */
static void test_weapon_shape_word(void){
    uint32_t ecx=0;setup();
    const uint32_t W=BASE+0x100u,D=BASE+0x1234u;
    w32(0,W+0x18u,D);w16(0,D,0xbeefu);
    const uint64_t p=es2_weapon_shape_word_registers(&memory,W,entry,&ecx);
    CHECK(lo(p)==0x9000beefu&&hi(p)==entry[1]&&ecx==entry[2]&&call_count==0);
}
/* mount_entry: [4a98e4] + 6 x sx(type word). */
static void test_mount_entry(void){
    uint32_t ecx=0;setup();
    const uint32_t T=BASE+0x8000u;w32(0,VA(0x4a98e4u),T);
    uint64_t p=es2_mount_entry_registers(&memory,&graph,0x12345u,entry,&ecx);
    CHECK(lo(p)==T+6u*0x2345u&&hi(p)==entry[1]&&ecx==entry[2]);
    p=es2_mount_entry_registers(&memory,&graph,0xfffeu,entry,NULL);
    CHECK(lo(p)==T-12u&&hi(p)==entry[1]);
    p=es2_mount_entry_registers(&memory,&graph,0x8000u,entry,NULL);
    CHECK(lo(p)==T-6u*0x8000u);
    CHECK(call_count==0);
}
/* mount_table: CX = word [[A+1F2h]] + word [4a9d70] in the entry ECX's high
 * word, pushed to mount_entry with EAX = A, EDX = [A+1F2h]. */
static void test_mount_table(void){
    uint32_t ecx=0;setup();
    const uint32_t A=BASE+0x100u,E=BASE+0x400u;
    w32(0,A+0x1f2u,E);w16(0,E,5);w16(0,VA(0x4a9d70u),0xfffdu);
    script(0,0x777u,0x888u,0x999u);
    uint64_t p=es2_mount_table_registers(&memory,&graph,A,entry,&ecx);
    CHECK(call_count==1&&observed==1&&calls[0].f==0x40fea0u&&calls[0].count==1);
    CHECK(calls[0].arg[0]==0xabcd0002u&&calls[0].eax==A&&calls[0].edx==E&&calls[0].ecx==0xabcd0002u);
    CHECK(lo(p)==0x777u&&hi(p)==0x888u&&ecx==0xabcd0002u);
    /* sum wraps in the low word */
    setup();w32(0,A+0x1f2u,E);w16(0,E,0xffffu);w16(0,VA(0x4a9d70u),2);script(0,1,2,3);
    p=es2_mount_table_registers(&memory,&graph,A,entry,&ecx);
    CHECK(calls[0].arg[0]==0xabcd0001u&&lo(p)==1&&hi(p)==2&&ecx==0xabcd0001u);
}
/* shield_split: word +0 = multiply_q10(sx +4, sx +6), word +2 = word +6
 * minus the new word +0; EAX keeps multiply_q10's high word. */
static void test_shield_split(void){
    uint32_t ecx=0;setup();
    const uint32_t S=BASE+0x200u;
    w16(0,S+4u,300);w16(0,S+6u,1000);script(0,0x12340123u,0x55u,0x66u);
    uint64_t p=es2_shield_split_registers(&memory,&graph,S,entry,&ecx);
    CHECK(call_count==1&&calls[0].f==0x47e0b0u&&calls[0].count==2&&calls[0].arg[0]==300&&calls[0].arg[1]==1000);
    CHECK(calls[0].eax==entry[0]&&calls[0].edx==entry[1]&&calls[0].ecx==entry[2]);
    CHECK(r16(0,S)==0x0123&&r16(0,S+2u)==709&&lo(p)==0x123402c5u&&hi(p)==0x55u&&ecx==0x66u);
    /* negative operands sign-extend; the word difference wraps */
    setup();w16(0,S+4u,0xff9cu);w16(0,S+6u,100);script(0,0x00019999u,7,8);
    p=es2_shield_split_registers(&memory,&graph,S,entry,NULL);
    CHECK(calls[0].arg[0]==0xffffff9cu&&calls[0].arg[1]==100);
    CHECK(r16(0,S)==0x9999&&r16(0,S+2u)==(uint16_t)(100-0x9999)&&lo(p)==(0x00010000u|(uint16_t)(100-0x9999))&&hi(p)==7);
}
/* energy_rate: A+256h = 20 plus the terms of the flags and the weapon. */
static void test_energy_rate(void){
    uint32_t ecx=0;
    const uint32_t A=BASE+0x1000u,W=BASE+0x3000u,D=BASE+0x3400u;
#define FLAGS(ab,aa) do{arena[A-BASE+0xabu]=(ab);arena[A-BASE+0xaau]=(aa);}while(0)
#define WEAPON(index) do{w32(0,A+0x313u,W);w32(0,W+0x18u,D);arena[D-BASE+0x17u]=(index);}while(0)
    /* nothing: 20, EAX = 0 (the weapon pointer), EDX = A+92h, ECX = entry */
    setup();FLAGS(0,0);w32(0,A+0x313u,0);
    uint64_t p=es2_energy_rate_registers(&memory,&graph,A,entry,&ecx);
    CHECK(call_count==0&&r16(0,A+0x256u)==0x14&&lo(p)==0&&hi(p)==A+0x92u&&ecx==entry[2]);
    /* byte +0ABh (before +0AAh): multiply_q10(200, 20) with EAX = EDX = A+92h */
    setup();FLAGS(1,1);w32(0,A+0x313u,0);script(0,0x100u,0x31u,0x32u);
    p=es2_energy_rate_registers(&memory,&graph,A,entry,&ecx);
    CHECK(call_count==1&&calls[0].f==0x47e0b0u&&calls[0].arg[0]==0xc8u&&calls[0].arg[1]==0x14u);
    CHECK(calls[0].eax==A+0x92u&&calls[0].edx==A+0x92u&&calls[0].ecx==entry[2]);
    CHECK(r16(0,A+0x256u)==0x100&&lo(p)==0&&hi(p)==0x31u&&ecx==0x32u);
    /* byte +0AAh only: multiply_q10(600, 20), the sum's low word is stored */
    setup();FLAGS(0,5);w32(0,A+0x313u,0);script(0,0x12345u,0x41u,0x42u);
    p=es2_energy_rate_registers(&memory,&graph,A,entry,NULL);
    CHECK(call_count==1&&calls[0].arg[0]==0x258u&&calls[0].arg[1]==0x14u&&r16(0,A+0x256u)==0x2345&&hi(p)==0x41u);
    /* the weapon: index byte -2 -> part 11h (the pushed ECX carries the high
     * word of [W+18h]); condition 48 -> quotient 0 -> multiply_q10(1024, 20)
     * = 80; 20 + 80 = 100 */
    setup();FLAGS(0,0);WEAPON(0xfe);script(0,0x30u,0x1u,0x2u);script(1,0x50u,0x61u,0x62u);
    p=es2_energy_rate_registers(&memory,&graph,A,entry,&ecx);
    CHECK(call_count==2&&calls[0].f==0x40dbc0u&&calls[0].count==2&&calls[0].arg[0]==A+0x206u&&calls[0].arg[1]==0x90000011u);
    CHECK(calls[0].eax==A+0x206u&&calls[0].edx==0xfffffffeu&&calls[0].ecx==0x90000011u);
    CHECK(calls[1].f==0x47e0b0u&&calls[1].arg[0]==0x400u&&calls[1].arg[1]==0x14u&&calls[1].eax==0x400u&&calls[1].edx==0x14u&&
        calls[1].ecx==0x400u);
    CHECK(r16(0,A+0x256u)==100&&lo(p)==0x50u&&hi(p)==0x61u&&ecx==0x62u);
    /* condition 224 -> quotient 4 -> 1024 - 816 = 208 */
    setup();FLAGS(0,0);WEAPON(3);script(0,0xe0u,0,0);script(1,0x10u,0,0);
    es2_energy_rate_registers(&memory,&graph,A,entry,NULL);
    CHECK(calls[0].arg[1]==0x90000016u&&calls[1].arg[0]==208u&&r16(0,A+0x256u)==0x14+0x10);
    /* condition -100: the quotient truncates to -1 -> 1024 + 204 */
    setup();FLAGS(0,0);WEAPON(0);script(0,0xffffff9cu,0,0);script(1,1,2,3);
    es2_energy_rate_registers(&memory,&graph,A,entry,NULL);
    CHECK(calls[0].arg[1]==0x90000013u&&calls[1].arg[0]==1228u&&r16(0,A+0x256u)==0x15);
    /* condition 225 and above skips the term; index byte -128 -> part FF93h;
     * EAX = part_ratio's EAX, EDX = its sign-extended AX, ECX = its ECX */
    setup();FLAGS(0,0);WEAPON(0x80);script(0,0x100e1u,0x7u,0x8u);
    p=es2_energy_rate_registers(&memory,&graph,A,entry,&ecx);
    CHECK(call_count==1&&calls[0].arg[1]==0x9000ff93u&&r16(0,A+0x256u)==0x14);
    CHECK(lo(p)==0x100e1u&&hi(p)==0xe1u&&ecx==0x8u);
    /* the largest condition below the limit */
    setup();FLAGS(0,0);WEAPON(0);script(0,0xe0u,0,0);script(1,0,0,0);
    es2_energy_rate_registers(&memory,&graph,A,entry,NULL);
    CHECK(call_count==2);
#undef FLAGS
#undef WEAPON
}
/* equip_slots: memset(record, 0, 20), then the weapons by class word. */
static void test_equip_slots(void){
    uint32_t ecx=0;setup();
    const uint32_t S=BASE+0x100u,R=BASE+0x200u,P=BASE+0x300u,WB=BASE+0x1000u,CB=BASE+0x2000u;
    memset(host(0,R),0xee,0x14);
    script(0,0,0,0);
    w32(0,S+4u,P);w16(0,S+8u,7);
    w32(0,P,WB);w32(0,P+4u,0);w32(0,P+8u,WB+0x100u);w32(0,P+12u,WB+0x200u);w32(0,P+16u,WB+0x300u);w32(0,P+20u,WB+0x400u);
    w32(0,P+24u,WB+0x500u);w16(0,S+8u,7);
    const uint32_t weapon[7]={WB,0,WB+0x100u,WB+0x200u,WB+0x300u,WB+0x400u,WB+0x500u};
    const uint16_t kinds[7]={0x12,0,0x13,0x1d,0x20,0x1f,0x12};
    for(unsigned i=0;i<7;++i)if(weapon[i]){w32(0,weapon[i]+0x1cu,CB+0x100u*i);w16(0,CB+0x100u*i+0x56u,kinds[i]);}
    uint64_t p=es2_equip_slots_registers(&memory,&graph,S,R,entry,&ecx);
    CHECK(call_count==1&&calls[0].f==0x49681au&&calls[0].count==3&&calls[0].arg[0]==R&&calls[0].eax==S+4u&&calls[0].edx==P&&
        calls[0].ecx==0xabcd0007u);
    CHECK(r32(0,R)==WB+0x500u&&r32(0,R+4u)==WB+0x200u&&r32(0,R+8u)==0&&r32(0,R+0xcu)==WB+0x300u&&r32(0,R+0x10u)==WB+0x400u);
    CHECK(hi(p)==0&&lo(p)==(WB+0x500u&0xffff0000u)&&ecx==0xffffu);
    /* a null array pointer ends the loop at once: EAX = the memset's EAX
     * with the old counter in AX */
    setup();w32(0,S+4u,0);w16(0,S+8u,3);script(0,0xaaaa0000u,0x5u,0x6u);
    memset(host(0,R),0xee,0x14);
    p=es2_equip_slots_registers(&memory,&graph,S,R,entry,&ecx);
    CHECK(lo(p)==0xaaaa0003u&&hi(p)==0&&ecx==2&&r32(0,R)==0);
    /* count 0: the old counter 0 gives EDX = 0 and EAX's low word 0 */
    setup();w32(0,S+4u,P);w16(0,S+8u,0);script(0,0xbbbb0009u,0x5u,0x6u);
    p=es2_equip_slots_registers(&memory,&graph,S,R,entry,&ecx);
    CHECK(lo(p)==0xbbbb0000u&&hi(p)==0&&ecx==0xffffu);
    /* a class below 12h or above 20h is not stored; classes 1Ah and 2Ah too */
    setup();w32(0,S+4u,P);w16(0,S+8u,3);w32(0,P,WB);w32(0,P+4u,WB+0x100u);w32(0,P+8u,WB+0x200u);
    w32(0,WB+0x1cu,CB);w16(0,CB+0x56u,0x11);w32(0,WB+0x11cu,CB+0x100u);w16(0,CB+0x156u,0x21);
    w32(0,WB+0x21cu,CB+0x200u);w16(0,CB+0x256u,0x1a);
    p=es2_equip_slots_registers(&memory,&graph,S,R,entry,NULL);
    for(unsigned i=0;i<5;++i)CHECK(r32(0,R+4u*i)==0);
}
/* node_state_fill. Tree (class ids): root 140007h with children A (14000Bh,
 * child G 140014h), B (140015h, array of 3 words, no children) and C
 * (140005h). Order of the class calls: root, A, G, B, C. */
static uint32_t node(uint32_t at,uint32_t class_id){
    w32(0,at,at+0x80u);w32(0,at+0x80u,METHOD+class_id);return at;
}
static void test_node_state_fill(void){
    uint32_t ecx=0;setup();
    const uint32_t root=node(BASE+0x1000u,0x140007u),A=node(BASE+0x1200u,0x14000bu),G=node(BASE+0x1400u,0x140014u),
        B=node(BASE+0x1600u,0x140015u),C=node(BASE+0x1800u,0x140005u),L1=BASE+0x2000u,L2=BASE+0x2100u,ARR=BASE+0x2200u;
    w16(0,root+0x10u,3);w32(0,root+0x12u,L1);w32(0,L1,A);w32(0,L1+4u,B);w32(0,L1+8u,C);
    w16(0,A+0x10u,1);w32(0,A+0x12u,L2);w32(0,L2,G);w16(0,G+0x10u,0);w16(0,B+0x10u,0);w16(0,C+0x10u,0);
    w16(0,B+0x16u,3);w32(0,B+0x1cu,ARR);w16(0,ARR+6u,0x1111);
    uint64_t p=es2_node_state_fill_registers(&memory,&graph,root,0x12340077u,entry,&ecx);
    CHECK(call_count==5&&observed==0);
    const uint32_t order[5]={root,A,G,B,C};
    for(unsigned i=0;i<5;++i)CHECK(calls[i].f==METHOD+(i==0?0x140007u:i==1?0x14000bu:i==2?0x140014u:i==3?0x140015u:0x140005u)&&
        calls[i].count==1&&calls[i].arg[0]==order[i]&&calls[i].eax==order[i]+0x80u);
    /* entry registers: the root's are the entry's; the children's are EDX =
     * the counter (0, 1, 2 over L1; 0 under A), ECX = the parent */
    CHECK(calls[0].edx==entry[1]&&calls[0].ecx==entry[2]);
    CHECK(calls[1].edx==0&&calls[1].ecx==root&&calls[2].edx==0&&calls[2].ecx==A);
    CHECK(calls[3].edx==1&&calls[3].ecx==root&&calls[4].edx==2&&calls[4].ecx==root);
    /* stores: A+4 (14000Bh), G+4 (140014h), the array words of B */
    CHECK(r16(0,A+4u)==0x77&&r16(0,G+4u)==0x77&&r16(0,root+4u)==0xcccc&&r16(0,B+4u)==0xcccc&&r16(0,C+4u)==0xcccc);
    CHECK(r16(0,ARR)==0x77&&r16(0,ARR+2u)==0x77&&r16(0,ARR+4u)==0x77&&r16(0,ARR+6u)==0x1111);
    /* exit: the root's child loop ends with EAX = count 3, EDX = 3, ECX = 0 */
    CHECK(lo(p)==3&&hi(p)==3&&ecx==0);
    /* the fill case reports ECX = node */
    setup();node(BASE+0x1000u,0x140015u);w16(0,BASE+0x1010u,0);w16(0,BASE+0x1016u,0);w32(0,BASE+0x101cu,ARR);
    p=es2_node_state_fill_registers(&memory,&graph,BASE+0x1000u,5,entry,&ecx);
    CHECK(ecx==BASE+0x1000u&&lo(p)==0&&hi(p)==0);
    /* classes without children: the compare chain's EAX and the virtual's EDX */
    static const struct { uint32_t class_id,eax; int stores; } flat[]={
        {0x140014u,0x140014u,1},{0x140005u,0xfffffffeu,0},{0x140006u,0xffffffffu,0},{0x1e0003u,0,0},{0x1e0005u,0,0},
        {0x1e0004u,0xffffffffu,0},{0x14000du,1,0}};
    for(unsigned i=0;i<7;++i){
        setup();const uint32_t n=node(BASE+0x1000u,flat[i].class_id);w16(0,n+4u,0x4242);w16(0,n+0x10u,0);w16(0,n+0x16u,0);
        p=es2_node_state_fill_registers(&memory,&graph,n,0x99u,entry,&ecx);
        CHECK(call_count==1&&r16(0,n+4u)==(flat[i].stores?0x99u:0x4242u)&&ecx==0);
        if(!(lo(p)==flat[i].eax&&hi(p)==0x5001u))fprintf(stderr,"flat %u got %08x:%08x\n",i,hi(p),lo(p));
        CHECK(lo(p)==flat[i].eax&&hi(p)==0x5001u);
    }
    /* 140016h: the chain above 140014h leaves c - 140015h - 9FFEEh - 2 */
    setup();p=es2_node_state_fill_registers(&memory,&graph,node(BASE+0x1000u,0x140016u),0x99u,entry,&ecx);
    CHECK(lo(p)==0x140016u-0x140015u-0x9ffeeu-2u&&hi(p)==0x5001u);
    /* 14000Ah: no case matches; the chain leaves c - 14000Ch */
    setup();p=es2_node_state_fill_registers(&memory,&graph,node(BASE+0x1000u,0x14000au),0x99u,entry,&ecx);
    CHECK(lo(p)==0x14000au-0x14000cu&&hi(p)==0x5001u);
    /* class 0: the wrapped chain */
    setup();p=es2_node_state_fill_registers(&memory,&graph,node(BASE+0x1000u,0u),0x99u,entry,&ecx);
    CHECK(lo(p)==0u-0x14000cu&&hi(p)==0x5001u);
    /* 14000Bh stores and walks zero children: EAX = 0 (count), EDX = 0 */
    setup();{const uint32_t n=node(BASE+0x1000u,0x14000bu);w16(0,n+0x10u,0);w16(0,n+4u,0x4242);
        p=es2_node_state_fill_registers(&memory,&graph,n,0xa5a50033u,entry,&ecx);
        CHECK(r16(0,n+4u)==0x33&&lo(p)==0&&hi(p)==0);}
    /* 140008h walks the children without storing; the child call gets the full value dword */
    setup();{const uint32_t n=node(BASE+0x1000u,0x140008u),c=node(BASE+0x1200u,0x140014u),L=BASE+0x2000u;
        w16(0,n+0x10u,1);w32(0,n+0x12u,L);w32(0,L,c);w16(0,c+4u,0x4242);w16(0,c+0x10u,0);w16(0,n+4u,0x4242);
        p=es2_node_state_fill_registers(&memory,&graph,n,0xa5a50033u,entry,&ecx);
        CHECK(r16(0,n+4u)==0x4242&&r16(0,c+4u)==0x33&&call_count==2&&lo(p)==1&&hi(p)==1);}
}
int main(void){
    test_weapon_shape_word();test_mount_entry();test_mount_table();test_shield_split();test_energy_rate();
    test_equip_slots();test_node_state_fill();
    printf("fit_leaves_test: %u checks passed\n",checks);return 0;
}
