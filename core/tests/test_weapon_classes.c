#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapon_classes.h"
/* Weapon-class methods (weapon_classes.md, section "the weapon-class methods"):
 * expectations from the reference listing over a synthetic arena (weapon W,
 * definition D, type T, record R, output word P, the frame pointers of
 * elf_step). Game calls are logged with their arguments and entry
 * registers and answer from fixed values. */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define W (ARENA+0x100)
#define D (ARENA+0x300)
#define T (ARENA+0x380)
#define R (ARENA+0x3c0)
#define P (ARENA+0x3e0)
#define FP (ARENA+0x400)
#define FQ (ARENA+0x440)
#define FC (ARENA+0x460)
static uint8_t arena[0x800];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"weapon_classes line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t counter_pointer[4];   /* the image global 6CBEE4h */
static void *host(void *c,uint32_t a){
    (void)c;if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    if(a-G(0x6cbee4)<4u)return counter_pointer+(a-G(0x6cbee4));
    fprintf(stderr,"weapon_classes: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; } logged;
static logged log_[16];static unsigned calls;
static uint32_t timer_result;
/* Answers of chosen callees (EAX) and the 24 bytes a widget write slot
 * receives. */
static uint32_t answer_f[3],answer_v[3];
static uint8_t seen[16][24];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16);logged *l=log_+calls++;memset(l,0,sizeof(*l));l->f=f;l->n=n;memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    r->eax=0xe0e00000u+calls;r->edx=0xd0d00000u+calls;r->ecx=0xc0c00000u+calls;
    if(f==G(0x467a48))r->eax=timer_result;
    for(unsigned i=0;i<3;++i)if(f==answer_f[i])r->eax=answer_v[i];
    if(f==0x5014u||f==0x5010u)memcpy(seen[calls-1],host(0,a[1]),24);
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,0,0,0,0,call,0,0,0};return g;}
#define S (ARENA+0x600)
static es2_weapon_graph graph_s(void){es2_weapon_graph g={I,S,0,0,0,call,0,0,0};return g;}
static const uint32_t e[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0,sizeof(arena));calls=0;timer_result=0;
    w32(0,W+0x1c,D);w32(0,W+0x20,T);w32(0,W+0x10,FP);w32(0,W+0x14,FQ);w32(0,FP+0x20,FC);
}
static uint32_t eax_of(uint64_t p){return (uint32_t)p;}
static uint32_t edx_of(uint64_t p){return (uint32_t)(p>>32);}

static void leaves(void){
    uint32_t c;uint64_t p;
    p=es2_weapon_zero_registers(e,&c);CHECK(eax_of(p)==0&&edx_of(p)==e[1]&&c==e[2]);
    p=es2_weapon_one_registers(e,&c);CHECK(eax_of(p)==0x11110001u&&edx_of(p)==e[1]&&c==e[2]);
    const uint32_t e2[3]={0xabcd1234u,1,2};p=es2_weapon_one_registers(e2,&c);CHECK(eax_of(p)==0xabcd0001u);
    reset();w8(R+0xd,0xfe);p=es2_weapon_trigger_registers(&memory,W,R,e,&c);CHECK(eax_of(p)==0xfe&&edx_of(p)==e[1]&&c==e[2]);
    reset();w32(0,W+0x77,0x1234);p=es2_gauge_match_registers(&memory,W,0x1234,e,&c);CHECK(p==((uint64_t)0x1234<<32|1)&&c==e[2]);
    p=es2_gauge_match_registers(&memory,W,0x1235,e,&c);CHECK(eax_of(p)==0&&edx_of(p)==0x1235);
    reset();w8(W+0x49,0x80);p=es2_beam_spent_registers(&memory,W,e,&c);CHECK(eax_of(p)==0xffffff80u&&edx_of(p)==W+0x43&&c==0xffffff80u);
    /* gun_spent: empty (EDX entry), ejected, ready. */
    reset();p=es2_gun_spent_registers(&memory,W,e,&c);CHECK(eax_of(p)==1&&edx_of(p)==e[1]);
    w16(0,W+0x7b,3);w8(W+0x49,1);p=es2_gun_spent_registers(&memory,W,e,&c);CHECK(eax_of(p)==1&&edx_of(p)==W+0x43);
    w8(W+0x49,0);p=es2_gun_spent_registers(&memory,W,e,&c);CHECK(eax_of(p)==0&&c==e[2]);
    /* gun_stock: 1/8 of the capacity, arithmetic shift of a negative word. */
    reset();w16(0,D+0x3a,80);w16(0,W+0x7b,10);p=es2_gun_stock_registers(&memory,W,e,&c);CHECK(eax_of(p)==1&&edx_of(p)==10&&c==10);
    w16(0,W+0x7b,9);p=es2_gun_stock_registers(&memory,W,e,&c);CHECK(eax_of(p)==0);
    w16(0,D+0x3a,0xfff1);w16(0,W+0x7b,0xfffe);p=es2_gun_stock_registers(&memory,W,e,&c);CHECK(c==0xfffffffeu&&eax_of(p)==1&&edx_of(p)==0xfffffffeu);
    /* beam_priority keeps the entry ECX's high word. */
    reset();w16(0,W+0x7b,0x77);p=es2_beam_priority_registers(&memory,W,e,&c);CHECK(eax_of(p)==0x33330077u&&c==0x33330077u&&edx_of(p)==W+0x43);
    w8(W+0x43,1);p=es2_beam_priority_registers(&memory,W,e,&c);CHECK(eax_of(p)==0x33332710u);
    /* Writers. */
    reset();w16(0,W+0x7b,9);p=es2_gun_clear_registers(&memory,W,e,&c);CHECK(r16(0,W+0x7b)==0&&eax_of(p)==W&&edx_of(p)==e[1]);
    reset();p=es2_beam_fill_registers(&memory,W,e,&c);CHECK(r8(W+0x43)==1&&r16(0,W+0x7b)==0x4b0&&edx_of(p)==W+0x43);
    reset();w8(W+0x49,1);es2_beam_preset_registers(&memory,W,e,&c);CHECK(r16(0,W+0x7b)==0);
    w8(W+0x49,0);es2_beam_preset_registers(&memory,W,e,&c);CHECK(r16(0,W+0x7b)==0x334);
    reset();w16(0,W+0x7b,5);w8(W+0x43,1);p=es2_beam_drain_registers(&memory,W,e,&c);
    CHECK(r16(0,W+0x7b)==0&&r8(W+0x43)==0&&r8(W+0x34)==1&&r8(W+0x3c)==1&&eax_of(p)==W+0x3b&&edx_of(p)==W+0x43);
    reset();w16(0,W+0x7d,5);w16(0,W+0x7b,5);p=es2_beam_clear_registers(&memory,W,e,&c);
    CHECK(r16(0,W+0x7d)==0&&r16(0,W+0x7b)==0&&r8(W+0x34)==1&&r8(W+0x3c)==1&&eax_of(p)==W+0x3b&&edx_of(p)==0);
    reset();w16(0,P,7);p=es2_weapon_ammo_registers(&memory,W,P,e,&c);CHECK(r16(0,P)==0&&eax_of(p)==((P&0xffff0000u)|5)&&edx_of(p)==e[1]);
    p=es2_weapon_ammo_registers(&memory,W,0,e,&c);CHECK(eax_of(p)==5);
}
static void gun_ammo(void){
    uint32_t c;uint64_t p;
    reset();w16(0,T,0);w16(0,T+2,0x4242);w16(0,W+0x7b,0x31);
    p=es2_gun_ammo_registers(&memory,W,P,e,&c);CHECK(r16(0,P)==0x31&&eax_of(p)==((T&0xffff0000u)|0x4242)&&edx_of(p)==P&&c==((T&0xffff0000u)|0x31));
    p=es2_gun_ammo_registers(&memory,W,0,e,&c);CHECK(c==T&&edx_of(p)==0);
    w16(0,T,1);w16(0,P,9);p=es2_gun_ammo_registers(&memory,W,P,e,&c);CHECK(r16(0,P)==0&&eax_of(p)==((P&0xffff0000u)|5)&&edx_of(p)==P&&c==T);
}
static void readiness(void){
    uint32_t c;uint64_t p;
    reset();w8(W+0x49,1);p=es2_gun_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==0&&c==e[2]);
    w8(W+0x49,0);w16(0,W+0x31,1);p=es2_gun_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==0&&c==0);
    w16(0,W+0x31,0);p=es2_gun_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==0&&c==1);
    w16(0,W+0x7b,1);p=es2_gun_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==1&&edx_of(p)==W+0x43);
    /* beam_ready: the required charge, EBX's high word when the ammunition
       word is the larger. */
    reset();w16(0,D+0x36,10);w16(0,D+0x38,20);w16(0,W+0x7b,15);w16(0,W+0x7d,15);
    p=es2_beam_ready_registers(&memory,W,e,0xbeef,&c);CHECK(eax_of(p)==1&&edx_of(p)==0xbeef000fu&&c==1);
    w16(0,W+0x7b,5);p=es2_beam_ready_registers(&memory,W,e,0xbeef,&c);CHECK(edx_of(p)==0x3333000au&&eax_of(p)==1);
    w16(0,W+0x7d,9);p=es2_beam_ready_registers(&memory,W,e,0,&c);CHECK(eax_of(p)==0);
    w16(0,D+0x36,30);w16(0,W+0x7d,20);p=es2_beam_ready_registers(&memory,W,e,0,&c);CHECK(edx_of(p)==((D&0xffff0000u)|20)&&eax_of(p)==1);
    w8(W+0x49,1);p=es2_beam_ready_registers(&memory,W,e,0,&c);CHECK(eax_of(p)==0&&c==W+0x43);
    /* elf_ready */
    reset();w16(0,D+0x36,10);w16(0,W+0x7b,4);w16(0,W+0x7d,10);
    p=es2_elf_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==((W&0xffff0000u)|1)&&c==((D&0xffff0000u)|10));
    w16(0,W+0x7d,9);p=es2_elf_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==0&&edx_of(p)==W+0x33);
    w8(W+0x33,1);w16(0,D+0x38,9);p=es2_elf_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==((W&0xffff0000u)|1)&&c==D&&edx_of(p)==(((W+0x33)&0xffff0000u)|9));
    w16(0,D+0x38,10);p=es2_elf_ready_registers(&memory,W,e,&c);CHECK(eax_of(p)==0);
}
static void triggers(void){
    uint32_t c;uint64_t p;es2_weapon_graph g=graph();
    /* elf_trigger: ELF2 sets both phases; a request returns its byte. */
    reset();w16(0,D+0x56,0x16);w8(R+0xd,3);p=es2_elf_trigger_registers(&memory,W,R,e,&c);
    CHECK(p==((uint64_t)3<<32|3)&&r8(W+0x47)==1&&r8(W+0x48)==1&&c==W+0x43);
    w8(R+0xd,0);w16(0,FQ,7);p=es2_elf_trigger_registers(&memory,W,R,e,&c);CHECK(p==0&&r8(W+0x48)==0&&r16(0,FQ)==0&&c==FQ);
    reset();w8(R+0xd,1);w16(0,W+0x84,9);p=es2_elf_trigger_registers(&memory,W,R,e,&c);CHECK(p==0&&r8(W+0x47)==1&&r16(0,W+0x84)==0&&c==W+0x43);
    w8(R+0xd,0);w16(0,FQ,7);p=es2_elf_trigger_registers(&memory,W,R,e,&c);CHECK(r8(W+0x47)==0&&r16(0,FQ)==0&&c==FQ);
    reset();w8(W+0x47,0xff);w8(R+0xd,1);p=es2_elf_trigger_registers(&memory,W,R,e,&c);CHECK(p==0&&c==0xffffffffu);
    /* laew_trigger */
    reset();w8(R+0xd,1);p=es2_laew_trigger_registers(&memory,&g,W,R,e,&c);CHECK(eax_of(p)==0&&edx_of(p)==W+0x43&&r8(W+0x46)==1&&r32(0,W+0x81)==20000&&calls==0);
    p=es2_laew_trigger_registers(&memory,&g,W,R,e,&c);CHECK(eax_of(p)==0&&r32(0,W+0x81)==21000&&edx_of(p)==R);
    w32(0,W+0x81,99500);p=es2_laew_trigger_registers(&memory,&g,W,R,e,&c);
    CHECK(eax_of(p)==1&&calls==1&&log_[0].f==G(0x45b734)&&log_[0].n==2&&log_[0].a[0]==1&&log_[0].a[1]==1&&log_[0].r[0]==100500&&log_[0].r[1]==R&&log_[0].r[2]==e[2]&&edx_of(p)==0xd0d00001u&&c==0xc0c00001u);
    calls=0;w8(R+0xd,0);p=es2_laew_trigger_registers(&memory,&g,W,R,e,&c);CHECK(eax_of(p)==1&&calls==0&&edx_of(p)==R);
}
static void steps(void){
    uint32_t c;uint64_t p;es2_weapon_graph g=graph();
    /* beam_step: ejected; transfer limited by a, the limit word and the
       deficit; PLAS doubles and halves. */
    reset();w8(W+0x49,1);p=es2_beam_step_registers(&memory,&g,W,0x55550010u,0,e,&c);CHECK(eax_of(p)==0x55550010u&&edx_of(p)==W+0x43&&calls==0);
    reset();w16(0,W+0x7b,100);w16(0,W+0x7d,40);w16(0,W+0x7f,30);
    p=es2_beam_step_registers(&memory,&g,W,0x55550050u,0x77770000u,e,&c);
    CHECK(calls==1&&log_[0].f==G(0x40ef94)&&log_[0].a[0]==W&&log_[0].a[1]==0x55550050u&&log_[0].a[2]==0x77770000u&&log_[0].r[0]==0&&log_[0].r[1]==W+0x43);
    CHECK(r16(0,W+0x7d)==70&&eax_of(p)==0x55550050u-30&&edx_of(p)==D&&c==0xc0c00001u);
    reset();w16(0,W+0x7b,100);w16(0,W+0x7d,98);w16(0,W+0x7f,30);
    p=es2_beam_step_registers(&memory,&g,W,0x55550050u,0,e,&c);CHECK(r16(0,W+0x7d)==100&&eax_of(p)==0x5555004eu);
    reset();w16(0,W+0x7b,100);w16(0,W+0x7d,110);w8(W+0x43,1);
    p=es2_beam_step_registers(&memory,&g,W,0x55550050u,0,e,&c);CHECK(r8(W+0x43)==0&&r16(0,W+0x7d)==105&&eax_of(p)==0x55550055u);
    reset();w16(0,D+0x56,0x19);w16(0,W+0x7b,100);w16(0,W+0x7d,90);w16(0,W+0x7f,30);
    p=es2_beam_step_registers(&memory,&g,W,0x50u,0,e,&c);CHECK(r16(0,W+0x7d)==100&&c==10&&eax_of(p)==0x50u-20);
    /* turbo_step */
    reset();w32(0,W+0x81,1);w32(0,W+0x7d,0x23);w8(W+0x49,1);
    p=es2_turbo_step_registers(&memory,&g,W,0x66660009u,5,e,&c);CHECK(r32(0,W+0x81)==0&&r32(0,W+0x7d)==0&&eax_of(p)==0x66660009u&&edx_of(p)==e[1]&&calls==0);
    reset();w32(0,W+0x7d,0x7c0);p=es2_turbo_step_registers(&memory,&g,W,0x66660030u,0x77770005u,e,&c);
    CHECK(calls==1&&log_[0].a[2]==0x22220005u&&log_[0].r[0]==W+0x43&&log_[0].r[1]==0x22220005u);
    CHECK(r32(0,W+0x7d)==0x7d0&&eax_of(p)==0x66660030u-0x10&&edx_of(p)==0xd0d00001u);
    reset();p=es2_turbo_step_registers(&memory,&g,W,0x66660003u,0,e,&c);CHECK(r32(0,W+0x7d)==3&&eax_of(p)==0x66660000u);
    /* elf_step: the timer path that advances the frame word, then
       beam_step through the bridge. */
    reset();w8(W+0x47,1);w16(0,FP+0x24,1);w16(0,FC,4);w16(0,FQ,1);w16(0,W+0x84,9);timer_result=0x12340000u;
    p=es2_elf_step_registers(&memory,&g,W,0x50u,0x77770006u,e,&c);
    CHECK(calls==2&&log_[0].f==G(0x467a48)&&log_[0].a[0]==W+0x83&&log_[0].r[0]==W+0x83&&log_[0].r[1]==W+0x43);
    CHECK(r16(0,FQ)==2&&r16(0,W+0x84)==0&&log_[1].f==G(0x40f00c)&&log_[1].a[2]==((W+0x10)&0xffff0000u|6)&&log_[1].r[0]==FQ&&log_[1].r[1]==2&&log_[1].r[2]==log_[1].a[2]);
    CHECK(eax_of(p)==0xe0e00002u&&edx_of(p)==0xd0d00002u&&c==0xc0c00002u);
    reset();w8(W+0x47,1);w16(0,FP+0x24,1);w16(0,FC,4);w16(0,FQ,3);timer_result=0;
    es2_elf_step_registers(&memory,&g,W,0x50u,6,e,&c);CHECK(r8(W+0x48)==1&&r8(W+0x47)==0&&log_[1].r[0]==W+0x43&&log_[1].r[1]==W+0x43&&log_[1].r[2]==6&&log_[1].a[2]==6);
    reset();w8(W+0x47,1);timer_result=1;es2_elf_step_registers(&memory,&g,W,0x50u,6,e,&c);CHECK(log_[1].r[1]==0&&log_[1].r[2]==((W+0x83)&0xffff0000u|6));
}
static void adjust(void){
    uint32_t c;uint64_t p;
    reset();w16(0,W+0x7b,0x480);p=es2_beam_adjust_registers(&memory,W,1,e,&c);
    CHECK(r16(0,W+0x7b)==0x4b0&&c==0x333304b0u&&edx_of(p)==0x222204d0u&&eax_of(p)==W+0x3b&&r8(W+0x34)==1&&r8(W+0x3c)==1);
    reset();w16(0,W+0x7b,0x30);p=es2_beam_adjust_registers(&memory,W,0x10000,e,&c);CHECK(r16(0,W+0x7b)==0&&c==0&&edx_of(p)==0x2221ffe0u);
    reset();w16(0,W+0x7b,0x100);p=es2_beam_adjust_registers(&memory,W,0,e,&c);CHECK(r16(0,W+0x7b)==0xb0&&c==0x222100b0u);
}
/* Constructors: the counter at CNT, [6CBEE4h] = CNT; the call stub
 * does not write memory, so [W+10h] and [W+1Ch] keep what the test puts
 * there. */
#define CNT (ARENA+0x600)
#define MNT (ARENA+0x640)
#define ACT (ARENA+0x680)
static void counter_reset(void){w32(0,G(0x6cbee4),CNT);}
static int logged_is(unsigned i,uint32_t va,unsigned n,uint32_t a0,uint32_t a1,uint32_t eax,uint32_t edx,uint32_t ecx){
    const logged *l=log_+i;return l->f==G(va)&&l->n==n&&l->a[0]==a0&&(n<2||l->a[1]==a1)&&l->r[0]==eax&&l->r[1]==edx&&l->r[2]==ecx;
}
static void ctors(void){
    uint32_t c;uint64_t p;es2_weapon_graph g=graph();
    /* A laser on hardpoint 2 with both track words and a shape block. */
    reset();counter_reset();w8(MNT+6,2);w16(0,MNT+2,5);w16(0,MNT+4,7);w32(0,W+0x10,FP);w8(W+0x30,0x5a);
    p=es2_weapon_ctor_registers(&memory,&g,W,ACT,MNT,D,T,e,&c);
    CHECK(calls==11&&logged_is(0,0x40fab0,2,D,2,2,2,D));
    CHECK(logged_is(1,0x402fc0,2,W+0x10,0xe0e00001u,W+0x10,CNT,CNT));
    CHECK(logged_is(2,0x402374,2,ACT,(CNT&0xffff0000u)|5,(CNT&0xffff0000u)|5,D,T));
    CHECK(logged_is(3,0x402374,2,ACT,0xd0d00007u,0xe0e00003u,0xd0d00007u,0xc0c00003u));
    CHECK(logged_is(4,0x40f540,1,W,0,0xe0e00004u,0xd0d00004u,0xc0c00004u));
    CHECK(logged_is(5,0x40dd4c,2,FP,0xe0e00005u,0xe0e00005u,0xd0d00005u,W));
    CHECK(logged_is(6,0x49681a,3,W+0x24,0,W+0x24,0xd0d00006u,0xc0c00006u)&&log_[6].a[2]==6);
    CHECK(logged_is(7,0x49681a,3,W+0x2a,0,0xe0e00007u,W+0x2a,0xc0c00007u)&&log_[7].a[2]==6);
    CHECK(logged_is(8,0x49681a,3,W+0x33,0,0xe0e00008u,0xd0d00008u,0xc0c00008u)&&log_[8].a[2]==8);
    CHECK(logged_is(9,0x49681a,3,W+0x3b,0,W+0x3b,0xd0d00009u,0xc0c00009u)&&log_[9].a[2]==8);
    CHECK(logged_is(10,0x49681a,3,W+0x43,0,W+0x43,0xd0d0000au,0xc0c0000au)&&log_[10].a[2]==0x18);
    CHECK(eax_of(p)==W&&edx_of(p)==CNT&&c==0xc0c0000bu&&r32(0,CNT)==4);
    CHECK(r32(0,W)==G(0x498e44)&&r32(0,W+0x18)==MNT&&r32(0,W+0x1c)==D&&r32(0,W+0x20)==T);
    CHECK(r32(0,W+0x5b)==0xe0e00003u&&r32(0,W+0x5f)==0xe0e00004u&&r8(W+0x4c)==1&&r16(0,W+0x63)==0x400&&r8(W+0x30)==0x5a);
    /* Hardpoint 4 (no shape), negative track words, no block. */
    reset();counter_reset();w32(0,W+0x10,0);w8(MNT+6,4);w16(0,MNT+2,0x8000);w16(0,MNT+4,0xffff);
    p=es2_weapon_ctor_registers(&memory,&g,W,ACT,MNT,D,T,e,&c);
    CHECK(calls==6&&logged_is(0,0x402fc0,2,W+0x10,0,W+0x10,CNT,CNT)&&log_[1].f==G(0x49681a)&&log_[1].r[1]==D&&log_[1].r[2]==T);
    CHECK(r32(0,W+0x5b)==0&&r32(0,W+0x5f)==0&&edx_of(p)==CNT&&c==0xc0c00006u&&r32(0,CNT)==4);
    /* A negative hardpoint byte loads a shape (signed compare). */
    reset();counter_reset();w8(MNT+6,0xff);w16(0,MNT+2,0xffff);w16(0,MNT+4,0xffff);
    es2_weapon_ctor_registers(&memory,&g,W,ACT,MNT,D,T,e,&c);CHECK(logged_is(0,0x40fab0,2,D,0xffffffffu,0xffffffffu,0xffffffffu,D));
    /* BEAM */
    reset();counter_reset();p=es2_beam_ctor_registers(&memory,&g,W,ACT,MNT,D,T,e,&c);
    CHECK(calls==2&&logged_is(0,0x40df30,5,W,ACT,ACT,D,MNT)&&log_[0].a[2]==MNT&&log_[0].a[3]==D&&log_[0].a[4]==T);
    CHECK(logged_is(1,0x47e0b0,2,0x334,0x4b0,0xe0e00001u,0xd0d00001u,0xc0c00001u));
    CHECK(eax_of(p)==W&&edx_of(p)==0&&c==CNT&&r32(0,CNT)==1&&r32(0,W)==G(0x498dcc));
    CHECK(r16(0,W+0x7b)==2&&r16(0,W+0x7d)==2&&r8(W+0x34)==1&&r8(W+0x3c)==1&&r32(0,W+0x77)==0&&r32(0,W+0x7f)==0x14);
    /* GUN: the capacity word of [W+1Ch], sign-extended and shifted. */
    reset();counter_reset();w32(0,W+0x1c,R);w16(0,R+0x3a,0xff80);w32(0,W+0x77,0xffffffffu);
    p=es2_gun_ctor_registers(&memory,&g,W,ACT,MNT,D,T,e,&c);
    CHECK(calls==1&&logged_is(0,0x40df30,5,W,ACT,ACT,D,MNT)&&r32(0,W)==G(0x498d54));
    CHECK(eax_of(p)==W&&edx_of(p)==0xffff8000u&&c==0&&r16(0,W+0x7b)==0xff80&&r32(0,W+0x7d)==0xffff8000u&&r32(0,W+0x77)==0&&r32(0,CNT)==1);
    /* EQUIP */
    reset();counter_reset();w8(W+0x4c,1);p=es2_equip_ctor_registers(&memory,&g,W,ACT,MNT,D,e,&c);
    CHECK(calls==1&&logged_is(0,0x40df30,5,W,ACT,D,MNT,ACT)&&log_[0].a[3]==D&&log_[0].a[4]==0);
    CHECK(eax_of(p)==W&&edx_of(p)==CNT&&c==0xc0c00001u&&r32(0,W)==G(0x498cdc)&&r8(W+0x4c)==0&&r16(0,W+0x77)==0xffff&&r32(0,CNT)==1);
    /* The five equipment classes over equip_ctor. */
    static const struct { uint32_t vt,code; int pointer_in_edx; } k[5]={{0x498c64,0x12,0},{0x498bec,0x1f,0},{0x498b74,0x1d,1},{0x498afc,0x1e,1},{0x498a84,0x20,1}};
    for(unsigned i=0;i<5;++i){
        reset();counter_reset();for(unsigned j=0x77;j<0x86;++j)w8(W+j,0xee);
        p=i==0?es2_ecm_ctor_registers(&memory,&g,W,ACT,MNT,D,e,&c):i==1?es2_turbo_ctor_registers(&memory,&g,W,ACT,MNT,D,e,&c):
          i==2?es2_targ_ctor_registers(&memory,&g,W,ACT,MNT,D,e,&c):i==3?es2_shield_ctor_registers(&memory,&g,W,ACT,MNT,D,e,&c):
          es2_energy_ctor_registers(&memory,&g,W,ACT,MNT,D,e,&c);
        CHECK(calls==1&&logged_is(0,0x40e234,5,W,ACT,D,MNT,ACT)&&log_[0].a[4]==0&&r32(0,W)==G(k[i].vt)&&r16(0,W+0x77)==k[i].code&&r32(0,W+0x79)==0);
        CHECK(eax_of(p)==W&&r32(0,CNT)==1&&(k[i].pointer_in_edx?edx_of(p)==CNT&&c==0xc0c00001u:edx_of(p)==0&&c==CNT));
        CHECK(r16(0,W+0x7d)==(i==1?0x7d0:0xeeee)&&r16(0,W+0x7f)==(i<2?0:0xeeee)&&r32(0,W+0x81)==(i==1?0:0xeeeeeeeeu));
    }
}

/* The gauge syncs and the LAEW emission. The widget WID has the
 * table VT whose slots +10h, +14h, +18h are the fake addresses 5010h, 5014h
 * and 5018h; the read slot answers the 8-byte record REC8, the getters the
 * 23-byte record REC23. */
#define WID (ARENA+0x700)
#define VT (ARENA+0x740)
#define REC8 (ARENA+0x680)
#define REC23 (ARENA+0x6a0)
static void widget_reset(void){
    reset();memset(seen,0,sizeof(seen));for(unsigned i=0;i<3;++i)answer_f[i]=answer_v[i]=0;
    w32(0,WID,VT);w32(0,VT+0x10,0x5010);w32(0,VT+0x14,0x5014);w32(0,VT+0x18,0x5018);
    for(unsigned i=0;i<8;++i)w8(REC8+i,(uint8_t)(0x11*(i+1)));
    for(unsigned i=0;i<23;++i)w8(REC23+i,(uint8_t)(0xa0+i));
    answer_f[0]=0x5018;answer_v[0]=REC8;
}
static void syncs(void){
    uint32_t c;uint64_t p;es2_weapon_graph g=graph_s();
    /* equip_sync: the record with byte 1 replaced by W+49h, handed to slot +14h. */
    widget_reset();w32(0,W+0x79,WID);w8(W+0x49,0xab);
    p=es2_equip_sync_registers(&memory,&g,W,e,&c);
    CHECK(calls==2&&log_[0].f==0x5018&&log_[0].n==1&&log_[0].a[0]==WID&&log_[0].r[0]==WID&&log_[0].r[1]==VT&&log_[0].r[2]==e[2]);
    CHECK(log_[1].f==0x5014&&log_[1].n==2&&log_[1].a[0]==WID&&log_[1].a[1]==S&&log_[1].r[0]==WID&&log_[1].r[1]==VT&&log_[1].r[2]==S);
    CHECK(seen[1][0]==0x11&&seen[1][1]==0xab&&seen[1][2]==0x33&&seen[1][7]==0x88);
    CHECK(eax_of(p)==0xe0e00002u&&edx_of(p)==0xd0d00002u&&c==0x88776655u);
    /* ecm_sync: a changed toggle posts the cockpit message 2Ah (nonzero) or 2Bh, words W+7Dh/+7Fh follow. */
    widget_reset();w32(0,W+0x79,WID);w16(0,W+0x7d,5);w8(REC8,3);
    p=es2_ecm_sync_registers(&memory,&g,W,e,&c);
    CHECK(calls==3&&log_[1].f==G(0x420a68)&&log_[1].n==1&&log_[1].a[0]==0x2a&&log_[1].r[0]==((REC8&0xffffff00u)|3)&&log_[1].r[1]==0x2a&&log_[1].r[2]==3);
    CHECK(r16(0,W+0x7d)==3&&r16(0,W+0x7f)==3&&seen[2][0]==3&&log_[2].f==0x5014&&log_[2].r[2]==S&&c==0x88776655u);
    widget_reset();w32(0,W+0x79,WID);w16(0,W+0x7d,5);w8(REC8,0);
    es2_ecm_sync_registers(&memory,&g,W,e,&c);CHECK(calls==3&&log_[1].a[0]==0x2b&&r16(0,W+0x7d)==0);
    widget_reset();w32(0,W+0x79,WID);w16(0,W+0x7d,0xffff);w8(REC8,0xff);          /* -1 against 255: still a change */
    es2_ecm_sync_registers(&memory,&g,W,e,&c);CHECK(calls==3&&log_[1].a[0]==0x2a);
    widget_reset();w32(0,W+0x79,WID);w16(0,W+0x7d,0xff);w8(REC8,0xff);            /* equal: no message */
    es2_ecm_sync_registers(&memory,&g,W,e,&c);CHECK(calls==2&&r16(0,W+0x7f)==0xff);
    /* turbo_sync: the latch rises above 600 (sound 2Ch), falls at or below 0, the widget byte is cleared with the latch. */
    widget_reset();w32(0,W+0x79,WID);w32(0,W+0x7d,0x259);w8(REC8,1);
    p=es2_turbo_sync_registers(&memory,&g,W,e,&c);
    CHECK(calls==3&&log_[1].f==G(0x462830)&&log_[1].a[0]==0x2c&&log_[1].r[0]==REC8&&log_[1].r[2]==0x88776655u&&r32(0,W+0x81)==1);
    CHECK(seen[2][0]==1&&r32(0,S+4)==0x259&&c==0x259);
    widget_reset();w32(0,W+0x79,WID);w32(0,W+0x7d,0x258);w8(REC8,1);es2_turbo_sync_registers(&memory,&g,W,e,&c);
    CHECK(calls==2&&r32(0,W+0x81)==0&&seen[1][0]==0);                              /* no latch: the byte is cleared */
    widget_reset();w32(0,W+0x79,WID);w32(0,W+0x7d,0xfffffff0u);w32(0,W+0x81,1);w8(REC8,1);es2_turbo_sync_registers(&memory,&g,W,e,&c);
    CHECK(calls==2&&r32(0,W+0x81)==0&&r32(0,W+0x7d)==0&&seen[1][0]==0);
    widget_reset();w32(0,W+0x79,WID);w32(0,W+0x7d,100);w32(0,W+0x81,1);w8(REC8,1);es2_turbo_sync_registers(&memory,&g,W,e,&c);
    CHECK(r32(0,W+0x81)==1&&seen[1][0]==1);
    widget_reset();w32(0,W+0x79,WID);w32(0,W+0x7d,100);w32(0,W+0x81,7);w8(REC8,0);es2_turbo_sync_registers(&memory,&g,W,e,&c);
    CHECK(r32(0,W+0x81)==0&&seen[1][0]==0);
    /* beam_sync: levels scaled by 1024/1200; W+34h clear takes the widget's level into W+7Bh. */
    widget_reset();w32(0,W+0x77,WID);w8(W+0x34,1);w16(0,W+0x7b,600);w16(0,W+0x7d,1200);w8(W+0x43,0x77);w8(W+0x49,0x66);
    answer_f[1]=G(0x440e9c);answer_v[1]=REC23;
    p=es2_beam_sync_registers(&memory,&g,W,0xa1,0xb2,0xc3,e,&c);
    CHECK(calls==2&&log_[0].f==G(0x440e9c)&&log_[0].a[0]==WID&&log_[0].r[0]==WID&&log_[0].r[1]==e[1]&&log_[0].r[2]==e[2]);
    CHECK(log_[1].f==0x5010&&log_[1].a[0]==WID&&log_[1].r[0]==WID&&log_[1].r[1]==VT&&log_[1].r[2]==S);
    CHECK(r32(0,S)==1024&&r32(0,S+4)==512&&seen[1][0x12]==0x77&&seen[1][0x11]==0x66&&seen[1][0x14]==0xa1&&seen[1][0x15]==0xb2&&seen[1][0x16]==0xc3);
    CHECK(seen[1][8]==0xa8);CHECK(seen[1][19]==0xa0+19);CHECK(r16(0,W+0x7b)==600);CHECK(eax_of(p)==0xe0e00002u);CHECK(c==0xc0c00002u);
    widget_reset();w32(0,W+0x77,WID);w8(W+0x34,0);w16(0,W+0x7d,0xfc00);
    answer_f[1]=G(0x440e9c);answer_v[1]=REC23;w32(0,REC23+4,1024);
    es2_beam_sync_registers(&memory,&g,W,0,0,0,e,&c);
    CHECK(r16(0,W+0x7b)==1200&&r32(0,S+4)==1024&&r32(0,S)==(uint32_t)(-1024*1024/1200));
    /* gun_sync: the shown value ramps down by movement_step(250) to ammunition*256, never below it. */
    widget_reset();w32(0,W+0x77,WID);w16(0,W+0x7b,3);w32(0,W+0x7d,1000);w8(W+0x49,0x5a);
    answer_f[1]=G(0x4414cc);answer_v[1]=REC23;answer_f[2]=G(0x467924);answer_v[2]=100;
    p=es2_gun_sync_registers(&memory,&g,W,1,2,3,e,&c);
    CHECK(calls==3&&log_[0].f==G(0x4414cc)&&log_[1].f==G(0x467924)&&log_[1].a[0]==250&&log_[1].r[0]==REC23&&log_[1].r[1]==0xd0d00001u&&log_[1].r[2]==0);
    CHECK(r32(0,W+0x7d)==900&&r32(0,S)==3&&seen[2][0x11]==0x5a&&seen[2][0x14]==1&&seen[2][0x15]==2&&seen[2][0x16]==3);
    CHECK(log_[2].f==0x5010&&log_[2].r[0]==VT&&log_[2].r[1]==S&&log_[2].r[2]==WID&&c==0xc0c00003u&&eax_of(p)==0xe0e00003u);
    widget_reset();w32(0,W+0x77,WID);w16(0,W+0x7b,3);w32(0,W+0x7d,700);answer_f[1]=G(0x4414cc);answer_v[1]=REC23;
    es2_gun_sync_registers(&memory,&g,W,0,0,0,e,&c);CHECK(calls==2&&r32(0,W+0x7d)==768&&r32(0,S)==3);   /* below the target: raised to it */
    widget_reset();w32(0,W+0x77,WID);w16(0,W+0x7b,3);w32(0,W+0x7d,1000);answer_f[1]=G(0x4414cc);answer_v[1]=REC23;answer_f[2]=G(0x467924);answer_v[2]=0xffff;
    es2_gun_sync_registers(&memory,&g,W,0,0,0,e,&c);CHECK(r32(0,W+0x7d)==1001);                /* the step word is signed */
    /* laew_sync: gun_sync with the words; the entry EDX/ECX keep their high words in the getter call. */
    widget_reset();w32(0,W+0x77,WID);w16(0,W+0x7b,3);w32(0,W+0x7d,768);answer_f[1]=G(0x4414cc);answer_v[1]=REC23;
    p=es2_laew_sync_registers(&memory,&g,W,0x1a1,0x2b2,0x3c3,e,&c);
    CHECK(calls==2&&seen[1][0x14]==0xa1&&seen[1][0x15]==0xb2&&seen[1][0x16]==0xc3);
    CHECK(log_[0].r[0]==WID&&log_[0].r[1]==0x222201a1u&&log_[0].r[2]==0x333302b2u);
}
static void laew_fire(void){
    uint32_t c;uint64_t p;es2_weapon_graph g=graph_s();
    /* The emission: charge down by one on a zero free word, the flag byte W+46h cleared, the five arguments. */
    reset();w32(0,W+0x81,0x1234);w16(0,W+0x7b,5);w8(W+0x46,9);w16(0,T+2,0x4321);
    p=es2_laew_fire_registers(&memory,&g,W,ACT,0x10000,e,&c);
    CHECK(calls==2&&log_[0].f==G(0x40e788)&&log_[0].n==3&&log_[0].a[0]==W&&log_[0].a[1]==ACT&&log_[0].r[0]==e[0]&&log_[0].r[1]==e[1]&&log_[0].r[2]==e[2]);
    CHECK(r16(0,W+0x7b)==4&&r8(W+0x46)==0);
    CHECK(log_[1].f==G(0x40acac)&&log_[1].n==5&&log_[1].a[0]==(((W+0x43)&0xffff0000u)|0x4321)&&log_[1].a[1]==G(0x4a9854)&&log_[1].a[2]==G(0x4a9860)&&log_[1].a[3]==ACT);
    CHECK(log_[1].r[0]==log_[1].a[0]&&log_[1].r[1]==0x1234&&log_[1].r[2]==T&&eax_of(p)==0xe0e00002u&&edx_of(p)==0xd0d00002u&&c==0xc0c00002u);
    reset();w16(0,W+0x7b,5);w16(0,T+2,0);es2_laew_fire_registers(&memory,&g,W,ACT,1,e,&c);CHECK(r16(0,W+0x7b)==5);   /* a nonzero free word keeps the charge */
}

int main(void){
    leaves();gun_ammo();readiness();triggers();steps();adjust();ctors();syncs();laew_fire();
    printf("weapon_classes: %u checks passed\n",checks);
    return 0;
}
