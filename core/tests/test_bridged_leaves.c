#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/bridged_leaves.h"
/* Synthetic guest: an arena at 90000000h and a synthetic image base I whose
 * globals 0x49b088 (cockpit pointer), 0x4d24e6/0x4d24ea (player, camera),
 * 0x4d2518/0x4d2520 (view words) live in a small block. Game calls are
 * logged and answered by a stub. Expectations from the reference listing
 * (hud.md, section "the bridged leaves"). */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define GLOBALS (I+0x9b000u)
#define G2 (I+0xd2400u)
static uint8_t arena[0x2000],globals[0x100],globals2[0x200];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"bridged_leaves line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    if(a-GLOBALS<sizeof(globals))return globals+(a-GLOBALS);
    if(a-G2<sizeof(globals2))return globals2+(a-G2);
    fprintf(stderr,"bridged_leaves: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static const es2_guest_memory *m=&memory;
typedef struct call_record { uint32_t target,args[3],regs[3],ebx,esi,edi,ret;unsigned n,slot,observed; } call_record;
static call_record log_[8];static unsigned calls;
static es2_r_key_frame *tracked;
static void stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;CHECK(calls<8);call_record *v=log_+calls++;
    v->target=target;v->n=n;memcpy(v->args,a,4*n);v->regs[0]=r->eax;v->regs[1]=r->edx;v->regs[2]=r->ecx;
    if(tracked){v->ebx=tracked->call.ebx;v->esi=tracked->call.esi;v->edi=tracked->call.edi;v->ret=tracked->call.ret;}
    r->eax=0x11110000u+calls;r->edx=0x22220000u+calls;r->ecx=0x33330000u+calls;
}
static uint32_t observed_function;static unsigned observed_slot,observations;
static void observe(void *context,uint32_t function,unsigned slot){(void)context;observed_function=function;observed_slot=slot;++observations;}
static const es2_weapon_graph graph={I,0,0,0,0,stub,NULL,observe,NULL};
static const uint32_t entry[3]={0xabcd1234u,0x5678ef01u,0x0badf00du};
#define EAX(p) ((uint32_t)(p))
#define EDX(p) ((uint32_t)((p)>>32))
static void weapon_word(void){
    const uint32_t W=ARENA+0x100,T=ARENA+0x300;uint32_t ecx=0;
    w16(0,W+0x7d,0x1234);w16(0,T+0x1a8,0);
    uint64_t p=es2_weapon_target_word_registers(m,W,T,entry,&ecx);
    CHECK(r16(0,W+0x7d)==0&&EAX(p)==W&&EDX(p)==T&&ecx==entry[2]);
    w16(0,T+0x1a8,1);p=es2_weapon_target_word_registers(m,W,T,entry,&ecx);CHECK(r16(0,W+0x7d)==0xffff);
    w16(0,W+0x7d,0);p=es2_weapon_target_word_registers(m,W,0,entry,&ecx);CHECK(r16(0,W+0x7d)==0xffff&&EDX(p)==0);
}
static void list_steps(void){
    const uint32_t L=ARENA+0x400,X=ARENA+0x500,T=ARENA+0x600;uint32_t ecx=0;
    w32(0,L,T);w16(0,L+6,5);for(unsigned i=0;i<8;++i)w32(0,T+4*i,0xa0000000u+i);
    w16(0,X+0x4b,4);uint64_t p=es2_list_next_registers(m,L,X,entry,&ecx);CHECK(EAX(p)==0xa0000000u&&EDX(p)==0&&ecx==L);
    w16(0,X+0x4b,1);p=es2_list_next_registers(m,L,X,entry,&ecx);CHECK(EAX(p)==0xa0000002u&&EDX(p)==2);
    w16(0,X+0x4b,0);p=es2_list_prev_registers(m,L,X,entry,&ecx);CHECK(EAX(p)==0xa0000004u&&EDX(p)==4&&ecx==L);
    w16(0,X+0x4b,3);p=es2_list_prev_registers(m,L,X,entry,&ecx);CHECK(EAX(p)==0xa0000002u&&EDX(p)==2);
    /* A negative index word: the IDIV remainder keeps the dividend's sign. */
    w16(0,X+0x4b,0xfff9);w32(0,T-8,0xbbbbbbbbu);   /* -7 + 1 = -6, rem 5 = -1 */
    p=es2_list_next_registers(m,L,X,entry,&ecx);CHECK(EDX(p)==0xffffffffu&&EAX(p)==r32(0,T-4));
    w16(0,X+0x4b,0xfff0);p=es2_list_prev_registers(m,L,X,entry,&ecx);   /* -16 + 5 - 1 = -12, rem 5 = -2 */
    CHECK(EDX(p)==0xfffffffeu&&EAX(p)==0xbbbbbbbbu);
}
static void r_key(void){
    const uint32_t P=ARENA+0x700,H=ARENA+0x900,E=ARENA+0xa00,VT=ARENA+0xa80,F=ARENA+0xc00+0x18,TOP=ARENA+0xc40;
    w32(0,GLOBALS+0x88,H);w32(0,H+0x20b,E);w32(0,E,VT);w32(0,VT,0x77777777u);
    for(unsigned pass=0;pass<4;++pass){
        uint8_t *p=arena+0x700;p[0xa3]=(uint8_t)(pass&1);p[0x96]=(uint8_t)(pass&2?1:0);
        const unsigned on=(p[0x96]^(pass&1))!=0;
        memset(arena+0xc00,0x5a,0x18);calls=0;observations=0;uint32_t ecx=0;
        es2_r_key_frame frame={{0,0,0,0},pass>=2?TOP:0};w32(0,TOP-4,0x4444u);
        tracked=pass>=2?&frame:NULL;
        const uint64_t r=es2_r_key_toggle_registers(m,&graph,pass>=2?&frame:NULL,F,0x3c3c3c3cu,P,entry,&ecx);
        tracked=NULL;
        CHECK(p[0x96]==(on?1:0)&&calls==4&&observations==4);
        CHECK(log_[0].target==I+0x62830u&&log_[0].args[0]==(on?0x1au:0x1bu)&&log_[0].regs[0]==P&&log_[0].regs[1]==P+0x92u&&log_[0].regs[2]==0);
        const uint32_t popped=pass>=2?0x4444u:(on?0x1au:0x1bu);
        CHECK(log_[1].target==I+0x35ac8u&&log_[1].args[0]==E&&log_[1].args[1]==0&&log_[1].args[2]==0x2cu&&log_[1].regs[2]==popped);
        CHECK(log_[1].regs[0]==0x11110001u&&log_[1].regs[1]==0x22220001u);
        CHECK(log_[2].target==I+0x35ac8u&&log_[2].args[2]==0x2du&&log_[2].regs[2]==0x33330002u);
        CHECK(log_[3].target==0x77777777u&&log_[3].args[0]==E&&log_[3].args[1]==F-0x18u&&log_[3].regs[0]==F-0x18u&&log_[3].regs[1]==VT&&
              log_[3].regs[2]==0x33330003u&&observed_slot==0x20bu&&observed_function==0x77777777u);
        CHECK(r16(0,F-0x18)==(on?0x2cu:0x2du)&&r32(0,F-0x16)==0&&arena[0xc00+6]==0x5a&&arena[0xc00+0x17]==0x5a);
        CHECK(EAX(r)==0x11110004u&&EDX(r)==0x22220004u&&ecx==0x33330004u);
        if(pass>=2)for(unsigned j=0;j<4;++j)CHECK(log_[j].ebx==E&&log_[j].esi==P+0x92u&&log_[j].edi==0x3c3c3c3cu);
        if(pass>=2)CHECK(log_[0].ret==I+(on?0x1b4a7u:0x1b4b7u)&&log_[1].ret==I+0x1b4c8u&&log_[2].ret==I+0x1b4d5u&&log_[3].ret==I+0x1b4e1u);
    }
}
static void nav_marker(void){
    const uint32_t H=ARENA+0x900,P=ARENA+0xd00;uint32_t ecx=0;
    w32(0,GLOBALS+0x88,H);w32(0,H+0x203,P);w32(0,P+0x26,0x101);w32(0,P+0x2a,0x202);w32(0,P+0x2e,0x303);
    arena[0x900+0x26a]=0;arena[0x900+0x26b]=1;
    const uint64_t p=es2_nav_marker_set_registers(m,I,entry,&ecx);
    CHECK(r32(0,H+0x25e)==0x101&&r32(0,H+0x262)==0x202&&r32(0,H+0x266)==0x303&&arena[0x900+0x26a]==1&&arena[0x900+0x26b]==0);
    CHECK(EAX(p)==H+0x25eu&&EDX(p)==H&&ecx==0x202);
}
static void throttle(void){
    const uint32_t G=ARENA+0x1000;uint32_t ecx=0;uint64_t p;
    w32(0,G+0x68,3);w32(0,G+0x18,0x10);w32(0,G+0x1c,0x20);w32(0,G+0x20,0x10);arena[0x1000+0xc1]=0;w32(0,G+0xb5,0);
    p=es2_throttle_handler_registers(m,G,0x10,5,entry,&ecx);CHECK(EAX(p)==0xfffffffbu&&EDX(p)==G+0x18u&&ecx==G&&r32(0,G+0xb5)==0xfffffffbu);
    arena[0x1000+0xc1]=1;p=es2_throttle_handler_registers(m,G,0x10,5,entry,&ecx);CHECK(EAX(p)==5&&r32(0,G+0xb5)==5);
    w32(0,G+0xb5,0);p=es2_throttle_handler_registers(m,G,0x20,5,entry,&ecx);CHECK(EAX(p)==0&&EDX(p)==G+0x1cu&&r32(0,G+0xb5)==0);
    p=es2_throttle_handler_registers(m,G,0x30,5,entry,&ecx);CHECK(EAX(p)==3&&EDX(p)==G+0x24u);
    w32(0,G+0x68,0x80000000u);p=es2_throttle_handler_registers(m,G,0x10,5,entry,&ecx);CHECK(EAX(p)==0&&EDX(p)==G+0x18u&&r32(0,G+0xb5)==0);
}
static void view_toggle(void){
    uint32_t ecx=0;const uint32_t C=ARENA+0x1200;
    w16(0,G2+0x118,0);w32(0,G2+0xea,C);w32(0,C+0x32,0x55);w32(0,G2+0xe6,0x55);w16(0,G2+0x120,0);
    uint64_t p=es2_view_pref_toggle_registers(m,I,entry,&ecx);
    CHECK(EAX(p)==1&&r16(0,G2+0x118)==1&&EDX(p)==C&&ecx==0x55&&r16(0,G2+0x120)==2);
    w16(0,G2+0x120,0);w32(0,G2+0xe6,0x56);p=es2_view_pref_toggle_registers(m,I,entry,&ecx);
    CHECK(EAX(p)==0&&r16(0,G2+0x118)==0&&r16(0,G2+0x120)==0);
    w16(0,G2+0x118,0x8000);p=es2_view_pref_toggle_registers(m,I,entry,&ecx);CHECK(EAX(p)==0&&r16(0,G2+0x118)==0);
}
static void constructors(void){
    uint32_t ecx=0;const uint32_t M=ARENA+0x1400;
    uint64_t p=es2_rotation_base_ctor_registers(M,entry,&ecx);CHECK(EAX(p)==M&&EDX(p)==entry[1]&&ecx==entry[2]);
    calls=0;p=es2_rotation_ctor_registers(&graph,M,entry,&ecx);
    CHECK(calls==1&&log_[0].target==I+0x9514fu&&log_[0].args[0]==M&&log_[0].regs[0]==entry[0]&&log_[0].regs[2]==entry[2]);
    CHECK(EAX(p)==M&&EDX(p)==0x22220001u&&ecx==M&&observed_function==0x49514fu);
    calls=0;p=es2_matrix_ctor_registers(&graph,M,entry,&ecx);
    CHECK(calls==2&&log_[0].target==I+0x95194u&&log_[0].args[0]==M&&log_[0].regs[1]==entry[1]);
    CHECK(log_[1].target==I+0x953c4u&&log_[1].args[0]==M+0x14u&&log_[1].regs[0]==M+0x14u&&log_[1].regs[1]==0x22220001u&&log_[1].regs[2]==M);
    CHECK(EAX(p)==M&&EDX(p)==0x22220002u&&ecx==M+0x14u&&observed_function==0x4953c4u);
}
int main(void){
    weapon_word();list_steps();r_key();nav_marker();throttle();view_toggle();constructors();
    printf("bridged_leaves: %u checks passed\n",checks);
    return 0;
}
