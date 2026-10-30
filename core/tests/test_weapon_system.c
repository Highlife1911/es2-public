#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapon_system.h"
/* Synthetic weapon systems for the cores. The image base is the
 * original's, so game calls arrive as their VAs; virtual slots are fake
 * addresses 0x7000xxxx. Every call is logged with its slots, the three
 * registers and the ESI value the core published; returns are scripted. */
#define BASE UINT32_C(0x10000000)
#define W_ (BASE+0x100u)
#define LIST (BASE+0x400u)
#define T0 (BASE+0x600u)
#define T1 (BASE+0x680u)
#define T2 (BASE+0x700u)
#define E_(k) (BASE+0x800u+0x80u*(k))
#define D_(k) (BASE+0xc00u+0x20u*(k))
#define C_(k) (BASE+0xd00u+0x40u*(k))
#define VT (BASE+0xf00u)
static uint8_t arena[0x50000];static unsigned checks;
typedef struct call_log { uint32_t f,a[2],r[3],esi; } call_log;
static call_log log_[64];static unsigned calls;static uint32_t published_esi,ret_eax[8],ret_edx[8],ret_ecx[8];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"weapon_system line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64&&n>=1&&n<=2);call_log *l=log_+calls;
    l->f=f;l->a[0]=a[0];l->a[1]=n>1?a[1]:0;l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;l->esi=published_esi;
    r->eax=ret_eax[calls%8];r->edx=ret_edx[calls%8];r->ecx=ret_ecx[calls%8];++calls;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={0x400000u,0,0,0,0,call,NULL,NULL,NULL};return g;}
static void reset(void){
    memset(arena,0,sizeof(arena));calls=0;published_esi=0xdeadbeefu;
    for(unsigned i=0;i<8;++i){ret_eax[i]=0x11110000u+i;ret_edx[i]=0x22220000u+i;ret_ecx[i]=0x33330000u+i;}
    w32(0,W_+4,LIST);w32(0,W_+0x1f,T0);w32(0,W_+0x25,T1);w32(0,W_+0x2b,T2);
    for(unsigned k=0;k<4;++k){w32(0,E_(k),VT);w32(0,E_(k)+0x18,D_(k));w32(0,E_(k)+0x1c,C_(k));w8(E_(k)+0x4c,1);}
    w32(0,VT+0x3c,0x7000003cu);w32(0,VT+0x48,0x70000048u);
}
/* Four slots: weapons 0..3; group table T0 holds w0 and w2. */
static void four(uint8_t sel,uint8_t group){
    w16(0,W_+8,4);w8(W_+0x1d,sel);w8(W_+0x1c,group);
    for(unsigned k=0;k<4;++k)w32(0,LIST+4*k,E_(k));
    w16(0,T0,1);w16(0,T0+4,1);w16(0,T1+6,7);
}
static void test_select_step(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={0xaabbccddu,0x12345678u,0x9abcdef0u};
    /* Nothing selected: the entry EAX's high bytes over the byte. */
    reset();four(0xff,0);uint64_t p=es2_select_step_registers(&memory,&g,W_,1,0x55550000u,entry);
    CHECK(calls==0&&(uint32_t)p==0xaabbccffu&&(uint32_t)(p>>32)==W_);
    /* From slot 0 forward: slot 1 is not a member, slot 2 is. */
    reset();four(0,0);p=es2_select_step_registers(&memory,&g,W_,1,0x55550000u,entry);
    CHECK(calls==1&&log_[0].f==0x410708u&&log_[0].a[0]==W_&&log_[0].a[1]==2);
    CHECK(log_[0].r[0]==2&&log_[0].r[1]==W_&&log_[0].r[2]==1);
    CHECK((uint32_t)p==ret_eax[0]&&(uint32_t)(p>>32)==ret_edx[0]);
    /* Backwards from slot 0: below 0 takes ESI - 1 with the caller's high word. */
    reset();four(0,0);es2_select_step_registers(&memory,&g,W_,0xffff,0x55550000u,entry);
    CHECK(log_[0].a[1]==0x55550002u);
    /* A ready-flag weapon whose definition byte is not positive is skipped. */
    reset();four(0,0);w8(E_(2)+0x4b,1);w8(D_(2)+0x16,0x80);es2_select_step_registers(&memory,&g,W_,1,0,entry);
    CHECK(log_[0].a[1]==0&&log_[0].r[2]==1);   /* wraps to slot 0, a member (found word 1) */
    reset();four(0,0);w8(E_(2)+0x4b,1);w8(D_(2)+0x16,1);es2_select_step_registers(&memory,&g,W_,1,0,entry);
    CHECK(log_[0].a[1]==2);
    /* An empty slot and a cleared E+4Ch are skipped; group 1 has only slot 3. */
    reset();four(1,1);w32(0,LIST+8,0);es2_select_step_registers(&memory,&g,W_,1,0,entry);
    CHECK(log_[0].a[1]==3&&log_[0].r[2]==7);
    reset();four(1,1);w8(E_(3)+0x4c,0);es2_select_step_registers(&memory,&g,W_,1,0,entry);
    CHECK(log_[0].a[1]==1&&log_[0].r[2]==1);
    /* Count 0: ESI - 1 borrows into the high word; index -1 is a member. */
    reset();w16(0,W_+8,0);w8(W_+0x1d,0);w32(0,LIST-4,E_(0));w16(0,T0-2,1);
    es2_select_step_registers(&memory,&g,W_,0xffff,0x55550000u,entry);
    CHECK(log_[0].a[1]==0x5554ffffu&&log_[0].r[2]==0xffff0001u);
}
static void test_fire_group(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={1,2,3};uint32_t esi=0;
    reset();four(2,1);g.context=NULL;
    uint64_t p=es2_fire_group_registers(&memory,&g,W_,0xabcd0001u,entry,&esi);
    CHECK(calls==0&&(uint32_t)p==W_&&(uint32_t)(p>>32)==1);
    /* To group 0 with slot 2 selected (a member): no step. */
    reset();four(2,1);p=es2_fire_group_registers(&memory,&g,W_,0xabcd0000u,entry,&esi);
    CHECK(arena[W_+0x1c-BASE]==0&&esi==3&&calls==2);
    CHECK(log_[0].f==0x4610adu&&log_[0].a[0]==0xabcd0000u&&log_[0].r[0]==W_&&log_[0].r[1]==T0&&log_[0].r[2]==2);
    CHECK(log_[1].f==0x441f90u&&log_[1].a[0]==ret_eax[0]&&log_[1].a[1]==0xabcd0000u&&log_[1].r[1]==ret_edx[0]);
    CHECK((uint32_t)p==ret_eax[1]&&(uint32_t)(p>>32)==ret_edx[1]);
    /* To group 2 (empty table): step 41074c(W, 1) first. */
    reset();four(1,0);p=es2_fire_group_registers(&memory,&g,W_,2,entry,&esi);
    CHECK(calls==3&&log_[0].f==0x41074cu&&log_[0].a[0]==W_&&log_[0].a[1]==1&&log_[0].r[0]==W_&&log_[0].r[1]==T2&&log_[0].r[2]==1);
    CHECK(esi==2&&log_[1].r[0]==ret_eax[0]&&log_[1].r[1]==ret_edx[0]&&log_[1].r[2]==ret_ecx[0]);
    /* Nothing selected: EDX = g, ECX = old group's sign over the byte. */
    reset();four(0xff,0xfe);p=es2_fire_group_registers(&memory,&g,W_,0x10000u,entry,&esi);
    CHECK(calls==2&&log_[0].r[1]==0x10000u&&log_[0].r[2]==0xffffffffu&&esi==0);
}
static void test_weapons_v3c(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={0,0,0xbeef1234u};
    reset();four(0,0);w32(0,LIST+4,0);uint64_t p=es2_weapons_v3c_registers(&memory,&g,W_,entry);
    CHECK(calls==3&&(uint32_t)p==0&&(uint32_t)(p>>32)==ret_edx[2]);
    CHECK(log_[0].f==0x7000003cu&&log_[0].a[0]==E_(0)&&log_[0].r[0]==VT&&log_[0].r[1]==E_(0)&&log_[0].r[2]==0xbeef0004u);
    CHECK(log_[1].a[0]==E_(2)&&log_[1].r[2]==((E_(0)&0xffff0000u)|2u));
    /* Count 0 or list 0: no call; EDX = the list pointer. */
    reset();w16(0,W_+8,0);p=es2_weapons_v3c_registers(&memory,&g,W_,entry);CHECK(calls==0&&p==((uint64_t)LIST<<32));
    reset();w16(0,W_+8,3);w32(0,W_+4,0);p=es2_weapons_v3c_registers(&memory,&g,W_,entry);CHECK(calls==0&&p==0);
    /* The count is unsigned: FFFFh slots, two weapons at the far end. */
    reset();w16(0,W_+8,0xffff);w32(0,W_+4,BASE+0x1000u);w32(0,BASE+0x1000u+4u*0xfffeu,E_(1));
    p=es2_weapons_v3c_registers(&memory,&g,W_,entry);CHECK(calls==1&&log_[0].r[2]==0xbeef0001u&&(uint32_t)(p>>32)==ret_edx[0]);
}
static void test_group_toggle(void){
    es2_weapon_graph g=graph();const uint32_t entry[3]={0,0,0};
    reset();four(0,0);for(unsigned k=0;k<4;++k){w8(D_(k)+7,(uint8_t)(k+1));w32(0,C_(k)+0x30,1);}
    w32(0,C_(1)+0x30,0);w8(D_(3)+7,2);
    uint64_t p=es2_group_toggle_registers(&memory,&g,W_,0x77770002u,entry);   /* slot 1 dead, slot 3 matches */
    CHECK(calls==2&&log_[0].f==0x410cd0u&&log_[0].a[0]==W_&&log_[0].a[1]==3&&log_[0].r[0]==3&&log_[0].r[1]==W_&&log_[0].r[2]==0x77770002u);
    CHECK(log_[1].f==0x70000048u&&log_[1].r[0]==VT&&log_[1].r[1]==ret_edx[0]&&log_[1].r[2]==ret_ecx[0]);
    CHECK((uint32_t)p==((ret_eax[1]&0xffff0000u)|1u)&&(uint32_t)(p>>32)==ret_edx[1]);
    CHECK(es2_group_toggle_ecx(&memory,W_,0x77770002u)==E_(3));
    /* V48 returns AX = 0: V3C follows. */
    reset();four(0,0);w8(D_(0)+7,0xff);w32(0,C_(0)+0x30,5);ret_eax[1]=0x12340000u;
    p=es2_group_toggle_registers(&memory,&g,W_,0xffff,entry);
    CHECK(calls==3&&log_[2].f==0x7000003cu&&log_[2].r[0]==0x12340000u&&log_[2].r[1]==VT&&log_[2].r[2]==E_(0));
    CHECK((uint32_t)p==((ret_eax[2]&0xffff0000u)|1u));
    /* No match: EAX = 0, EDX = W, ECX = n. */
    reset();four(0,0);p=es2_group_toggle_registers(&memory,&g,W_,9,entry);
    CHECK(calls==0&&p==((uint64_t)W_<<32)&&es2_group_toggle_ecx(&memory,W_,9)==9);
}
int main(void){
    test_select_step();test_fire_group();test_weapons_v3c();test_group_toggle();
    printf("weapon_system: %u checks passed\n",checks);return 0;
}
