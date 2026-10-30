#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/type_loads.h"
/* Synthetic guest: an arena at 90000000h and a synthetic image base I whose
 * data window I+98000h .. I+0AA000h holds the globals (slot words, the type
 * tables and their counts, the extension pointers 4A09F0h ..). Game calls are
 * logged and answered by a stub (EAX from a per-call answer, possibly 0, or
 * 11110001h + call; EDX 22220000h + call; ECX 33330000h + call); 49667c returns
 * its first argument when that is not 0. Expectations worked out by hand from
 * the reference listing (mission_load.md, section "MECH and FLYER type loads").
 * */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define WIN (I+0x98000u)
#define VA(x) (I+(uint32_t)(x)-0x400000u)
static uint8_t arena[0x4000],window[0x12000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"type_loads line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    if(a-WIN<sizeof(window))return window+(a-WIN);
    fprintf(stderr,"type_loads: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static const es2_guest_memory *m=&memory;
typedef struct call_record { uint32_t target,args[6],regs[3];unsigned n; } call_record;
static call_record log_[64];static unsigned calls;
static uint32_t answer_eax[64];static int answered[64];
static void answer(unsigned call,uint32_t eax){answer_eax[call]=eax;answered[call]=1;}
static void stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;CHECK(calls<64);call_record *v=log_+calls;
    v->target=target;v->n=n;if(n)memcpy(v->args,a,4*n);v->regs[0]=r->eax;v->regs[1]=r->edx;v->regs[2]=r->ecx;
    r->eax=answered[calls]?answer_eax[calls]:0x11110001u+calls;
    if(target==VA(0x49667c)&&a[0])r->eax=a[0];
    r->edx=0x22220000u+calls;r->ecx=0x33330000u+calls;
    ++calls;
}
static uint32_t observed[64][2];static unsigned observations;
static void observe(void *context,uint32_t function,unsigned slot){(void)context;observed[observations][0]=function;observed[observations][1]=slot;++observations;}
static es2_weapon_graph graph={I,0,0,0,0,stub,NULL,observe,NULL};
static const uint32_t entry[3]={0xabcd1234u,0x5678ef01u,0x0badf00du};
#define EAX(p) ((uint32_t)(p))
#define EDX(p) ((uint32_t)((p)>>32))
static int call_is(unsigned i,uint32_t target,unsigned n,uint32_t eax,uint32_t edx,uint32_t ecx){
    return log_[i].target==target&&log_[i].n==n&&log_[i].regs[0]==eax&&log_[i].regs[1]==edx&&log_[i].regs[2]==ecx;
}
static void reset(void){
    memset(arena,0x5a,sizeof(arena));memset(window,0,sizeof(window));calls=0;observations=0;
    memset(answer_eax,0,sizeof(answer_eax));memset(answered,0,sizeof(answered));
}
static void slot_bases(void){
    uint32_t ecx=0;
    reset();w16(0,VA(0x4a98e8),0x0007);
    uint64_t p=es2_gun_slot_base_registers(m,I,0x12340003u,entry,&ecx);
    CHECK(EAX(p)==0xabcd0007u&&EDX(p)==0x56780003u&&ecx==entry[2]&&r16(0,VA(0x4a98e8))==10);
    reset();w16(0,VA(0x4987dc),0xfffe);
    p=es2_col_slot_base_registers(m,I,1,entry,&ecx);
    CHECK(EAX(p)==0xabcdfffeu&&EDX(p)==0x56780001u&&r16(0,VA(0x4987dc))==0xffff);
    reset();w16(0,VA(0x498856),0xffff);
    p=es2_dmg_slot_base_registers(m,I,0xffff0002u,entry,&ecx);
    CHECK(EAX(p)==0xabcdffffu&&EDX(p)==0x56780002u&&r16(0,VA(0x498856))==1);
}
static void remap(void){
    const uint32_t P=ARENA+0x100,T=ARENA+0x200;uint32_t ecx=0;
    /* w = 3: the byte table[3] = F0h gives FFF0h; EDX = T, ECX = FFFFFFF0h */
    reset();w16(0,P,3);w8(T+3,0xf0);
    uint64_t p=es2_remap_word_registers(m,P,T,entry,&ecx);
    CHECK(EAX(p)==P&&EDX(p)==T&&ecx==0xfffffff0u&&r16(0,P)==0xfff0);
    /* w = -1: unchanged, EDX keeps its high word */
    reset();w16(0,P,0xffff);
    p=es2_remap_word_registers(m,P,T,entry,&ecx);
    CHECK(EAX(p)==P&&EDX(p)==0x5678ffffu&&ecx==entry[2]&&r16(0,P)==0xffff);
    /* the word at the table itself: w = 1 reads its own high byte 0 */
    reset();w16(0,T,0x0001);
    p=es2_remap_word_registers(m,T,T,entry,&ecx);
    CHECK(r16(0,T)==0x0000&&ecx==0&&EDX(p)==T);
}
static void classes(void){
    const uint32_t L=ARENA+0x100,E=ARENA+0x200,O=ARENA+0x400;uint32_t ecx=0;
    /* three entries: class bytes 2, 7, -1; words 11h, 22h, 33h */
    reset();w16(0,L,3);w32(0,L+2,E);
    w16(0,E,0x11);w8(E+6,2);w16(0,E+26,0x22);w8(E+26+6,7);w16(0,E+52,0x33);w8(E+52+6,0xff);
    uint64_t p=es2_gun_classes_registers(m,L,O,entry,&ecx);
    CHECK(r16(0,O)==0x11&&r16(0,O+2)==0xffff&&r16(0,O+4)==0x33&&r16(0,O+6)==0x5a5a);
    CHECK(EAX(p)==(((E+78)&0xffff0000u)|3u)&&EDX(p)==3&&ecx==O+6);
    /* a negative count: nothing written */
    reset();w16(0,L,0x8000);w32(0,L+2,E);
    p=es2_gun_classes_registers(m,L,O,entry,&ecx);
    CHECK(EAX(p)==((E&0xffff0000u)|0x8000u)&&EDX(p)==0&&ecx==O&&r16(0,O)==0x5a5a);
    /* the output over the count word: the count drops to 0 after one entry */
    reset();w16(0,L,2);w32(0,L+2,E);w16(0,E,0);w8(E+6,1);
    p=es2_gun_classes_registers(m,L,L,entry,&ecx);
    CHECK(EAX(p)==((E+26)&0xffff0000u)&&EDX(p)==1&&ecx==L+2);
}
static void node_list(void){
    const uint32_t L=ARENA+0x100,S=ARENA+0x200,VT=ARENA+0x300,C=ARENA+0x400,B=ARENA+0x500;uint32_t ecx=0;
    /* n = 3, classes 5, -1, 7: new[](12), two virtual +24h calls */
    reset();w32(0,S,VT);w32(0,VT+0x24,0x77770024u);w16(0,C,5);w16(0,C+2,0xffff);w16(0,C+4,7);
    answer(0,B);answer(1,0x0a0a0a0au);answer(2,0x0b0b0b0bu);
    uint64_t p=es2_node_list_registers(m,&graph,L,S,0xbeef0003u,C,entry,&ecx);
    CHECK(calls==3&&call_is(0,VA(0x4774f0),1,0xabcd0003u,0x56780003u,12)&&log_[0].args[0]==12);
    CHECK(call_is(1,0x77770024u,2,0x90000003u,0x90000005u,VT)&&log_[1].args[0]==S&&log_[1].args[1]==0x90000005u);
    CHECK(call_is(2,0x77770024u,2,1,0x22220007u,VT)&&log_[2].args[1]==0x22220007u);
    CHECK(r32(0,L)==B&&r16(0,L+4)==3&&r32(0,B)==0x0a0a0a0au&&r32(0,B+4)==0&&r32(0,B+8)==0x0b0b0b0bu&&r32(0,B+12)==0x5a5a5a5au);
    CHECK(EAX(p)==0x0b0b0000u&&EDX(p)==0x22220002u&&ecx==0x0003ffffu&&observations==3&&observed[1][1]==0x24u&&observed[0][1]==0);
    /* a null block ends the list before the first entry */
    reset();w32(0,S,VT);answer(0,0);
    p=es2_node_list_registers(m,&graph,L,S,2,C,entry,&ecx);
    CHECK(calls==1&&r32(0,L)==0&&EAX(p)==((L&0xffff0000u)|2u)&&EDX(p)==0&&ecx==0x00020001u);
    /* n = 0: new[](0), no entry; EAX's low word 0 over L */
    reset();answer(0,B);
    p=es2_node_list_registers(m,&graph,L,S,0,C,entry,&ecx);
    CHECK(calls==1&&EAX(p)==(L&0xffff0000u)&&EDX(p)==B&&ecx==0x0000ffffu);
}
static void select_types(void){
    uint32_t ecx=0;
    reset();for(unsigned i=0;i<0x58;++i)w8(VA(0x4a9d08)+i,0xff);w16(0,VA(0x4a9d70),0x5555);
    /* a new MECH type 3 (high word 1): index 0, count 1, the gun base word */
    uint64_t p=es2_mech_type_select_registers(m,&graph,0x00010003u,entry,&ecx);
    CHECK(calls==3&&call_is(0,VA(0x40feb4),1,3,0x5678ef00u,0x00010003u)&&log_[0].args[0]==1);
    CHECK(call_is(1,VA(0x40cbd4),1,0x11110001u,0x22220000u,1)&&call_is(2,VA(0x40d200),1,0x11110002u,0x22220001u,1));
    CHECK(r8(VA(0x4a9d08)+3)==0&&r16(0,VA(0x4a9d06))==1&&r16(0,VA(0x4a9d70))==0x0001&&EAX(p)==0x11110003u&&EDX(p)==0x22220002u&&ecx==1);
    /* the same type again: only the two slot reservations */
    calls=0;
    p=es2_mech_type_select_registers(m,&graph,3,entry,&ecx);
    CHECK(calls==2&&call_is(0,VA(0x40cbd4),1,0,entry[1],3)&&EAX(p)==0x11110002u&&ecx==1);
    /* a second new type: index 1, the base word stays */
    calls=0;w16(0,VA(0x4a9d06),1);
    p=es2_mech_type_select_registers(m,&graph,9,entry,&ecx);
    CHECK(calls==3&&r8(VA(0x4a9d08)+9)==1&&r16(0,VA(0x4a9d06))==2&&r16(0,VA(0x4a9d70))==0x0001&&log_[0].regs[1]==0x5678ef01u);
    /* FLYER: type 5 with the count 2 */
    reset();for(unsigned i=0;i<0x24;++i)w8(VA(0x4a9dcc)+i,0xff);w16(0,VA(0x4a9dca),2);
    p=es2_flyer_type_select_registers(m,&graph,5,entry,&ecx);
    CHECK(calls==2&&call_is(0,VA(0x40d200),1,5,0x5678ef02u,5)&&call_is(1,VA(0x40cbd4),1,0x11110001u,0x22220000u,1));
    CHECK(r8(VA(0x4a9dcc)+5)==2&&r16(0,VA(0x4a9dca))==3&&EAX(p)==0x11110002u&&EDX(p)==0x22220001u&&ecx==1);
    calls=0;
    p=es2_flyer_type_select_registers(m,&graph,5,entry,&ecx);
    CHECK(calls==0&&EAX(p)==2&&EDX(p)==entry[1]&&ecx==5);
}
static void walk(void){
    const uint32_t N=ARENA+0x100,VT=ARENA+0x300,T=ARENA+0x200,W=ARENA+0x400,K=ARENA+0x500;uint32_t ecx=0;
    #define CLASS(c) do{reset();w32(0,N,VT);w32(0,VT,0x77770000u);answer(0,(c));}while(0)
    /* TSGroup: remap N+4 */
    CLASS(0x140014u);
    uint64_t p=es2_node_remap_registers(m,&graph,N,T,9,entry,&ecx);
    CHECK(calls==2&&call_is(0,0x77770000u,1,VT,entry[1],entry[2])&&log_[0].args[0]==N&&observed[0][1]==0x100u);
    CHECK(call_is(1,VA(0x41ff68),2,0x140014u,0x22220000u,N)&&log_[1].args[0]==N+4&&log_[1].args[1]==T);
    CHECK(EAX(p)==0x11110002u&&EDX(p)==0x22220001u&&ecx==0x33330001u);
    /* TSBSPPart: words 0, 3, 9, 4 with limit 9 (table 3 -> 5, 4 -> -1), two children */
    CLASS(0x140015u);w16(0,N+0x16,4);w32(0,N+0x1c,W);w16(0,W,0);w16(0,W+2,3);w16(0,W+4,9);w16(0,W+6,4);
    w8(T+3,5);w8(T+4,0xff);w16(0,N+0x10,2);w32(0,N+0x12,K);w32(0,K,ARENA+0x600);w32(0,K+4,ARENA+0x640);
    p=es2_node_remap_registers(m,&graph,N,T,0x00050009u,entry,&ecx);
    CHECK(calls==4&&call_is(1,VA(0x41ff68),2,3,3,6)&&log_[1].args[0]==W+2);
    CHECK(call_is(2,VA(0x41ff88),3,0x90000009u,ARENA+0x600,0)&&log_[2].args[1]==T&&log_[2].args[2]==0x90000009u);
    CHECK(call_is(3,VA(0x41ff88),3,0x90000009u,ARENA+0x640,1)&&EAX(p)==N&&EDX(p)==2&&ecx==2);
    /* TSCellAnimPart: remap N+4, no child */
    CLASS(0x14000bu);w16(0,N+0x10,0);
    p=es2_node_remap_registers(m,&graph,N,T,9,entry,&ecx);
    CHECK(calls==2&&call_is(1,VA(0x41ff68),2,N+4,0x22220000u,N)&&EAX(p)==N&&EDX(p)==0&&ecx==0);
    /* the classes that end: 140009h (EAX d - 7), 1E0005h (0), 140006h (d - 2) */
    CLASS(0x140009u);p=es2_node_remap_registers(m,&graph,N,T,9,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0xfffffffdu&&EDX(p)==0x22220000u&&ecx==N);
    CLASS(0x1e0005u);p=es2_node_remap_registers(m,&graph,N,T,9,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0);
    CLASS(0x140006u);p=es2_node_remap_registers(m,&graph,N,T,9,entry,&ecx);
    CHECK(calls==1&&EAX(p)==0xffffffffu);
    /* ANShape and TSShape walk their children */
    CLASS(0x1e0003u);w16(0,N+0x10,1);w32(0,N+0x12,K);w32(0,K,ARENA+0x600);
    p=es2_node_remap_registers(m,&graph,N,T,0x7777000au,entry,&ecx);
    CHECK(calls==2&&call_is(1,VA(0x41ff88),3,0x9000000au,ARENA+0x600,0)&&EAX(p)==N&&EDX(p)==1&&ecx==1);
    CLASS(0x140008u);w16(0,N+0x10,0);p=es2_node_remap_registers(m,&graph,N,T,9,entry,&ecx);
    CHECK(calls==1&&EAX(p)==N&&EDX(p)==0&&ecx==0);
    #undef CLASS
}
static void lod(void){
    const uint32_t S=ARENA+0x100,A=ARENA+0x200,SH0=ARENA+0x300,SH1=ARENA+0x340,SH2=ARENA+0x380,B=ARENA+0x400,
        N0=ARENA+0x600,N1=ARENA+0x640,N0b=ARENA+0x680,N2=ARENA+0x6c0,P2A=ARENA+0x500,F=ARENA+0x1000,f=F+ES2_LOD_REMAP_FRAME;
    uint32_t ecx=0;es2_weapon_graph g=graph;g.scratch=F;
    reset();w32(0,S,A);w16(0,S+0xc,3);w32(0,A,SH0);w32(0,A+4,SH1);w32(0,A+8,SH2);
    w32(0,SH0,ARENA+0x700);w32(0,SH1,ARENA+0x740);w32(0,SH2,ARENA+0x780);
    w32(0,ARENA+0x720,0x77770020u);w32(0,ARENA+0x760,0x77770020u);w32(0,ARENA+0x7a0,0x77770020u);
    w8(B,2);w8(B+1,5);w8(B+2,0xff);w32(0,SH0+0x2a,P2A);w16(0,P2A+0x10,0x14);
    w16(0,N0+4,0x0107);w16(0,N1+4,4);w16(0,N0b+4,3);w16(0,N2+4,10);
    answer(1,N0);answer(2,N1);answer(3,0);answer(4,N0b);answer(5,0);answer(6,N2);
    uint64_t p=es2_lod_remap_registers(m,&g,S,B,entry,&ecx);
    CHECK(calls==9&&call_is(0,VA(0x49681a),3,B,f-0x1a0,entry[2])&&log_[0].args[0]==f-0x1a0&&log_[0].args[1]==0xffffffffu&&
          log_[0].args[2]==0x190);
    CHECK(call_is(1,0x77770020u,2,SH0,ARENA+0x700,A)&&log_[1].args[0]==SH0&&log_[1].args[1]==2);
    CHECK(call_is(2,0x77770020u,2,ARENA+0x740,2,SH1)&&log_[2].args[0]==SH1&&call_is(3,0x77770020u,2,ARENA+0x780,2,SH2));
    CHECK(call_is(4,0x77770020u,2,SH0,ARENA+0x700,A)&&log_[4].args[1]==5&&call_is(6,0x77770020u,2,ARENA+0x780,5,SH2));
    CHECK(r8(f-0x19c)==7&&r8(f-0x164)==3&&r32(0,f-8)==3&&r32(0,f-4)==B+2&&r16(0,f-0xe)==3);
    CHECK(call_is(7,VA(0x41ff88),3,SH1,f-0x1a0,f-0x1d2)&&log_[7].args[0]==SH1&&log_[7].args[1]==f-0x1a0&&log_[7].args[2]==0x90000014u);
    CHECK(call_is(8,VA(0x41ff88),3,SH2,f-0x16e,f-0x1d2)&&log_[8].args[2]==0x90000014u);
    CHECK(EAX(p)==0x11110009u&&EDX(p)==3&&ecx==0x33330008u);
    /* a single shape: nothing, the entry registers */
    reset();w16(0,S+0xc,1);
    p=es2_lod_remap_registers(m,&g,S,B,entry,&ecx);
    CHECK(calls==0&&EAX(p)==entry[0]&&EDX(p)==entry[1]&&ecx==entry[2]);
}
static void gun_list(void){
    const uint32_t F=ARENA+0x1000,f=F+ES2_GUN_LIST_LOAD_FRAME,S=f-0x30,NAME=ARENA+0x700,E=ARENA+0x800,BLK=ARENA+0x900;
    uint32_t ecx=0;es2_weapon_graph g=graph;g.scratch=F;
    reset();w32(0,S+4,0);w16(0,E,3);w32(0,f-0x50,0xfeedf00du);answer(2,E);answer(4,BLK);
    uint64_t p=es2_gun_list_load_registers(m,&g,NAME,4,0xabcd9999u,entry,&ecx);
    CHECK(calls==7&&call_is(0,VA(0x49630c),0,VA(0x498fb1),entry[1],entry[2])&&r16(0,f-0x40)==8);
    CHECK(call_is(1,VA(0x472f14),2,0x11110001u,S,0x33330000u)&&log_[1].args[0]==S&&log_[1].args[1]==NAME);
    CHECK(call_is(2,VA(0x40fea0),1,0x11110002u,0x22220001u,0x33330001u)&&log_[2].args[0]==0xabcd0004u);
    CHECK(call_is(3,VA(0x4735b8),3,0,0x22220002u,S)&&log_[3].args[1]==2&&log_[3].args[2]==E);
    CHECK(call_is(4,VA(0x49667c),5,3,0x22220003u,0x33330003u)&&log_[4].args[0]==0&&log_[4].args[1]==0x1a&&log_[4].args[2]==3&&
          log_[4].args[3]==1&&log_[4].args[4]==VA(0x4117a5));
    CHECK(call_is(5,VA(0x4735b8),3,3,78,S)&&log_[5].args[1]==78&&log_[5].args[2]==BLK&&r32(0,E+2)==BLK);
    CHECK(call_is(6,VA(0x472f64),2,E,S,0x33330005u)&&log_[6].args[0]==S&&log_[6].args[1]==2);
    CHECK(EAX(p)==E&&EDX(p)==0xfeedf00du&&ecx==0x33330006u);
    /* a failed open reports with the name; ECX the pushed type dword */
    reset();w32(0,S+4,5);w16(0,E,0xffff);answer(2,E);answer(5,BLK);
    p=es2_gun_list_load_registers(m,&g,NAME,0x77770002u,0x12345678u,entry,&ecx);
    CHECK(calls==8&&call_is(3,VA(0x479c78),6,5,0,0x12340002u)&&log_[3].args[0]==VA(0x6c56c8)&&log_[3].args[1]==5&&
          log_[3].args[2]==0&&log_[3].args[3]==VA(0x49921e)&&log_[3].args[4]==0x146&&log_[3].args[5]==NAME);
    CHECK(call_is(4,VA(0x4735b8),3,0x11110004u,0x22220003u,S)&&log_[5].args[2]==0xffffffffu&&log_[6].args[1]==0xffffffe6u);
}
static void mech_type(void){
    const uint32_t F=ARENA+0x3000,f=F+ES2_MECH_TYPE_LOAD_FRAME,NAMES=ARENA+0x2000,NAME=ARENA+0x2100,TYPES=ARENA+0x2200,
        rec=TYPES+0x21e,R=rec+2,S=rec+0xde,BLK6=ARENA+0x2800,E=ARENA+0x2900,SHAPES=ARENA+0x2a00,SH0=ARENA+0x2b00,SH1=ARENA+0x2b80,
        TAB=VA(0x4a9d72)+24;
    uint32_t ecx=0;es2_weapon_graph g=graph;g.scratch=F;
    /* type 2, index 1: two LOD shapes, scaled stats, debris, no "fm" */
    reset();w32(0,VA(0x4a9d60),NAMES);w32(0,NAMES+8,NAME);w8(VA(0x4a9d08)+2,1);w32(0,VA(0x4a9d64),TYPES);
    w32(0,VA(0x4a09f0),0x0d0a0001u);w32(0,VA(0x4a09f8),0x0d0a0002u);w32(0,VA(0x4a0a00),0x0d0a0003u);w16(0,VA(0x4a9d70),0x10);
    w16(0,R+4,1000);w16(0,R+0x4e,0);w16(0,R+0xc2,200);w16(0,R+0xc4,50);w16(0,R+2,300);w16(0,R+0x2c,(uint16_t)-100);w16(0,R+0x6c,7);
    w16(0,rec+0x96,3);w8(rec+0xce,'X');w32(0,S,SHAPES);w32(0,SHAPES,SH0);w32(0,SHAPES+4,SH1);w32(0,f-0x24,0xc0dec0deu);
    answer(4,0x7e7e0002u);answer(5,BLK6);answer(8,0x0000abcdu);answer(10,E);answer(11,0x55550003u);answer(14,0x0d0d0d0du);
    uint64_t p=es2_mech_type_load_registers(m,&g,2,entry,&ecx);
    CHECK(calls==16&&call_is(0,VA(0x49630c),0,VA(0x49a3b0),entry[1],entry[2]));
    CHECK(call_is(1,VA(0x492bec),2,0x0d0a0001u,NAME,2)&&log_[1].args[0]==NAME&&log_[1].args[1]==0x0d0a0001u);
    CHECK(call_is(2,VA(0x492f54),4,R,0x22220001u,0x33330001u)&&log_[2].args[0]==R&&log_[2].args[1]==0xd8&&log_[2].args[2]==1&&
          log_[2].args[3]==0x11110002u);
    CHECK(r16(0,rec)==1&&r16(0,rec+0x6c)==2&&call_is(3,VA(0x492bec),2,0x0d0a0002u,NAME,0x33330002u));
    CHECK(call_is(4,VA(0x474cd8),4,0x11110000u,0x22220003u,0x33330003u)&&log_[4].args[0]==S&&log_[4].args[1]==0x11110004u&&
          log_[4].args[2]==0x1e0003u&&log_[4].args[3]==0x11110000u&&r16(0,f-0x2c)==0);
    CHECK(r16(0,S+0xc)==2&&r32(0,S+4)==VA(0x49a344)&&call_is(5,VA(0x4774f0),1,0x7e7e0002u,2,16)&&log_[5].args[0]==16);
    CHECK(call_is(6,VA(0x49667c),5,2,BLK6,16)&&log_[6].args[0]==BLK6&&log_[6].args[1]==6&&log_[6].args[2]==2&&log_[6].args[3]==0x211&&
          log_[6].args[4]==VA(0x4207db)&&r16(0,f-0x14)==8&&r32(0,f-4)==BLK6&&r32(0,S+8)==BLK6);
    CHECK(call_is(7,VA(0x420090),2,BLK6,rec+0x30,0x33330006u)&&log_[7].args[0]==S&&log_[7].args[1]==rec+0x30);
    /* q = (400 x 50 << 16) / 200 = 640000h; multiply_q10(13Bh, 1000) */
    CHECK(r16(0,R+0xc6)==1000&&r32(0,f-0x30)==0x640000u&&r32(0,f-0x34)==1000&&call_is(8,VA(0x47e0b0),2,1000,0,1000)&&
          log_[8].args[0]==0x13b&&log_[8].args[1]==1000&&r16(0,R+0xc0)==0xabcd);
    CHECK(r16(0,R+2)==30000&&r16(0,R+4)==0x86a0&&r16(0,R+0x2c)==0xd8f0&&r16(0,R+0x6c)==700);
    CHECK(call_is(9,VA(0x492bec),2,0x0d0a0003u,NAME,7)&&call_is(10,VA(0x40fee8),2,0x11110011u,0x22220009u,0x33330009u)&&
          log_[10].args[0]==0x1111000au&&log_[10].args[1]==0x11110011u);
    CHECK(call_is(11,VA(0x40fc50),2,E,f-0x60,0x3333000au)&&log_[11].args[0]==E&&log_[11].args[1]==f-0x60&&r16(0,f-0x36)==3);
    CHECK(call_is(12,VA(0x40304c),4,BLK6,SH0,0)&&log_[12].args[0]==BLK6&&log_[12].args[1]==SH0&&log_[12].args[2]==0x22220003u&&
          log_[12].args[3]==f-0x60&&r32(0,SH0+0x26)==TAB);
    CHECK(call_is(13,VA(0x40304c),4,BLK6+6,SH1,1)&&log_[13].args[2]==0x90000003u&&r32(0,SH1+0x26)==TAB);
    CHECK(call_is(14,VA(0x4379cc),2,(TAB&0xffff0000u)|1u,SH1,NAME)&&log_[14].args[0]==NAME&&r32(0,rec+0xda)==0x0d0d0d0du);
    CHECK(call_is(15,VA(0x40874c),3,rec+0xce,TAB,rec+0xce)&&log_[15].args[0]==rec+0x212&&log_[15].args[1]==rec+0xce&&
          log_[15].args[2]==TAB&&r32(0,f-0x28)==rec+0xce);
    CHECK(EAX(p)==0xc0dec0deu&&EDX(p)==0x2222000fu&&ecx==0x3333000fu);
    /* the "fm" record instead of the scaling; no shape, a null array, no debris */
    reset();w32(0,VA(0x4a9d60),NAMES);w32(0,NAMES+8,NAME);w8(VA(0x4a9d08)+2,1);w32(0,VA(0x4a9d64),TYPES);
    w16(0,R+4,1000);w16(0,R+0x4e,1);w8(rec+0xce,0);w32(0,f-0x24,0xc0dec0deu);
    w32(0,rec+0x1dc+0x22,300);w32(0,rec+0x1dc+0x26,100);w32(0,rec+0x1dc+0x2a,50);w32(0,rec+0x1dc+0x2e,10);
    answer(4,0x7e7e0000u);answer(5,0);answer(9,0x55550000u);
    p=es2_mech_type_load_registers(m,&g,2,entry,&ecx);
    CHECK(calls==13&&call_is(5,VA(0x4774f0),1,0x7e7e0000u,0,4)&&r32(0,S+8)==0&&call_is(6,VA(0x420090),2,0,rec+0x30,4));
    CHECK(r16(0,R+0xc6)==1000&&call_is(7,VA(0x492bec),2,0,NAME,0x33330006u)&&call_is(10,VA(0x4379cc),2,0x55550001u,0x22220009u,NAME));
    CHECK(call_is(11,VA(0x492bec),2,NAME,0x2222000au,0x3333000au)&&log_[11].args[1]==VA(0x49a47e));
    CHECK(call_is(12,VA(0x492f54),4,0x1111000cu,0x2222000bu,0x3333000bu)&&log_[12].args[0]==rec+0x1dc&&log_[12].args[1]==0x36);
    CHECK(r32(0,rec+0x1dc+0xe)==0x50000u&&EAX(p)==0xc0dec0deu&&EDX(p)==0&&ecx==rec+0xce);
}
static void mech_table(void){
    const uint32_t F=ARENA+0x3000,f=F+ES2_MECH_TYPES_FRAME,NAMES=ARENA+0x100,TYPES=ARENA+0x400;
    uint32_t ecx=0;es2_weapon_graph g=graph;g.scratch=F;
    reset();w16(0,VA(0x4a9d06),2);for(unsigned i=0;i<0x58;++i)w8(VA(0x4a9d08)+i,0xff);w8(VA(0x4a9d08)+3,0);w8(VA(0x4a9d08)+7,1);
    w32(0,f-0x24,0x0f0f0f0fu);answer(1,NAMES);answer(2,TYPES);
    uint64_t p=es2_mech_types_registers(m,&g,entry,&ecx);
    CHECK(calls==7&&call_is(0,VA(0x49630c),0,VA(0x49a3e0),entry[1],entry[2]));
    CHECK(call_is(1,VA(0x492d00),3,0x11110001u,0x22220000u,0x33330000u)&&log_[1].args[0]==VA(0x49a481)&&log_[1].args[1]==0x55&&
          log_[1].args[2]==8&&r32(0,VA(0x4a9d60))==NAMES);
    CHECK(call_is(2,VA(0x4774f0),1,NAMES,2,0x440)&&log_[2].args[0]==0x440&&r32(0,f-4)==TYPES);
    CHECK(call_is(3,VA(0x49667c),5,2,TYPES,0x440)&&log_[3].args[1]==0x21e&&log_[3].args[2]==2&&log_[3].args[3]==0x211&&
          log_[3].args[4]==VA(0x420817)&&r16(0,f-0x14)==8&&r32(0,VA(0x4a9d64))==TYPES);
    CHECK(call_is(4,VA(0x4378d8),1,TYPES,0x22220002u,0x33330003u)&&log_[4].args[0]==0x22220002u);
    CHECK(call_is(5,VA(0x4201a8),1,0,0x22220004u,3)&&log_[5].args[0]==3&&call_is(6,VA(0x4201a8),1,1,0x22220005u,7));
    CHECK(EAX(p)==0x0f0f0f0fu&&EDX(p)==0x22220006u&&ecx==0x54);
    /* a null array: no constructor call, [4A9D64h] = 0 */
    reset();w16(0,VA(0x4a9d06),1);for(unsigned i=0;i<0x58;++i)w8(VA(0x4a9d08)+i,0xff);answer(2,0);
    p=es2_mech_types_registers(m,&g,entry,&ecx);
    CHECK(calls==4&&call_is(3,VA(0x4378d8),1,0,0x22220001u,0x21eu+4u)&&r32(0,VA(0x4a9d64))==0&&ecx==0x54&&EDX(p)==0x22220003u);
}
static void flyers(void){
    const uint32_t FN=ARENA+0x100,NAME=ARENA+0x200,FT=ARENA+0x400,rec=FT+0xe0,fm=rec+0x3a,SH=ARENA+0x800;uint32_t ecx=0;
    /* no FLYER type: nothing */
    reset();
    uint64_t p=es2_flyer_types_registers(m,&graph,entry,&ecx);
    CHECK(calls==0&&EAX(p)==entry[0]&&EDX(p)==entry[1]&&ecx==entry[2]);
    /* one FLYER type, t = 4 */
    reset();w16(0,VA(0x4a9dca),1);for(unsigned i=0;i<0x24;++i)w8(VA(0x4a9dcc)+i,0xff);w8(VA(0x4a9dcc)+4,0);answer(0,FN);answer(1,FT);
    p=es2_flyer_types_registers(m,&graph,entry,&ecx);
    CHECK(calls==3&&call_is(0,VA(0x492d00),3,entry[0],entry[1],entry[2])&&log_[0].args[0]==VA(0x49a6a3)&&log_[0].args[1]==0x22&&
          log_[0].args[2]==4&&r32(0,VA(0x4a9df0))==FN);
    CHECK(call_is(1,VA(0x49667c),5,1,0x22220000u,0x33330000u)&&log_[1].args[0]==0&&log_[1].args[1]==0x70&&log_[1].args[2]==1&&
          log_[1].args[3]==1&&log_[1].args[4]==VA(0x423098)&&r32(0,VA(0x4a9df4))==FT);
    CHECK(call_is(2,VA(0x422ed0),1,0,0x22220001u,4)&&log_[2].args[0]==4&&EAX(p)==0xff&&EDX(p)==0x22220002u&&ecx==0x21);
    /* one FLYER type load, t = 1 with index 2 */
    reset();w32(0,VA(0x4a9df0),FN);w32(0,FN+4,NAME);w8(VA(0x4a9dcc)+1,2);w32(0,VA(0x4a9df4),FT);
    w32(0,VA(0x4a09f0),0x0d0a0001u);w32(0,VA(0x4a09f8),0x0d0a0002u);w32(0,VA(0x4a0a04),0x0d0a0004u);w32(0,VA(0x4a0a08),0x0d0a0005u);
    w32(0,fm+0x22,1000);w32(0,fm+0x26,0);w32(0,fm+0x2a,0xfffffffdu);w32(0,fm+0x2e,1);answer(3,SH);
    p=es2_flyer_type_load_registers(m,&graph,1,entry,&ecx);
    CHECK(calls==10&&call_is(0,VA(0x492bec),2,0x0d0a0001u,1,1)&&log_[0].args[0]==NAME&&log_[0].args[1]==0x0d0a0001u);
    CHECK(call_is(1,VA(0x492f54),4,0x11110001u,0x22220000u,0x33330000u)&&log_[1].args[0]==rec&&log_[1].args[1]==0x2e&&
          log_[1].args[3]==0x11110001u);
    CHECK(call_is(2,VA(0x492bec),2,0x11110002u,0x0d0a0002u,0x33330001u)&&call_is(3,VA(0x474c38),1,0x11110003u,0x22220002u,0x33330002u));
    CHECK(r32(0,rec+0x36)==SH&&r32(0,SH+0x26)==VA(0x4a9d8a)&&call_is(4,VA(0x492bec),2,0x0d0a0004u,0x22220003u,0x11110003u));
    CHECK(call_is(5,VA(0x40d160),1,0x11110005u,0x22220004u,0x33330004u)&&r32(0,rec+0x2e)==0x11110006u);
    CHECK(call_is(6,VA(0x492bec),2,0x11110006u,0x0d0a0005u,0x11110005u)&&call_is(7,VA(0x40cd88),1,0x11110007u,0x22220006u,0x33330006u));
    CHECK(r32(0,rec+0x32)==0x11110008u&&call_is(8,VA(0x492bec),2,0x11110008u,0x22220007u,0x11110007u)&&log_[8].args[1]==VA(0x49a6a0));
    CHECK(call_is(9,VA(0x492f54),4,0x11110009u,0x22220008u,0x33330008u)&&log_[9].args[0]==fm&&log_[9].args[1]==0x36);
    CHECK(r32(0,fm+0xe)==0xff060000u&&EAX(p)==0xff060000u&&EDX(p)==0&&ecx==0x33330009u);
}
int main(void){
    slot_bases();remap();classes();node_list();select_types();walk();lod();gun_list();mech_type();mech_table();flyers();
    printf("type_loads: %u checks passed\n",checks);
    return 0;
}
