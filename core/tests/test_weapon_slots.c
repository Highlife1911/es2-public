#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapon_slots.h"
/* Synthetic guest: objects at 90000000h, the image globals (4d1a5c..,
 * 4d2526/4d2528, 49b088) at B+RVA. Game callees are logged with their
 * arguments, entry registers and the saved EBX/ESI/EDI, and return planned
 * registers. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
static uint8_t arena[8192],image_globals[0x40000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"weapon_slots line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-(B+0x9b000u)<sizeof(image_globals))return image_globals+(a-(B+0x9b000u));
    fprintf(stderr,"weapon_slots: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t *b8(uint32_t a){return host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[4],r[3],s[3]; unsigned n,slot; } logged;
static logged log_[32];static unsigned calls,observed;
static uint32_t ret[32][3],last_slot;
static es2_weapon_slots_saved saved;
/* Selections that a logged 41074c call stores into W+1Dh (steps > 0). */
static uint8_t steps[8];static unsigned step_count;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32&&n<=4);
    if(f==B+0x1074cu&&step_count){b8(a[0])[0x1d]=steps[0];memmove(steps,steps+1,sizeof(steps)-1);--step_count;}logged *l=log_+calls;l->f=f;l->n=n;l->slot=last_slot;memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;l->s[0]=saved.ebx;l->s[1]=saved.esi;l->s[2]=saved.edi;
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);last_slot=slot;++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,0,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){
    calls=observed=step_count=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));memset(arena,0,sizeof(arena));
    memset(image_globals,0,sizeof(image_globals));saved.ebx=0xb0b0b0b0u;saved.esi=0x5e5e5e5eu;saved.edi=0xd1d1d1d1u;
}
#define W (OBJ+0x100)
#define LIST (OBJ+0x200)
#define E0 (OBJ+0x400)
#define E1 (OBJ+0x500)
#define E2 (OBJ+0x600)
#define D0 (OBJ+0x700)
#define D1 (OBJ+0x720)
#define C0 (OBJ+0x780)
#define C1 (OBJ+0x7c0)
#define VT (OBJ+0x800)
#define T0 (OBJ+0x900)
#define H (OBJ+0xa00)
#define P (OBJ+0xb00)
#define REC (OBJ+0xc00)
#define G(rva) (B+(rva))

static void test_leaves(void){
    uint32_t ecx;reset();
    /* gauge_mode: the word only; EDX keeps the entry high word. */
    uint64_t r=es2_gauge_mode_registers(&memory,H,0xabcd0002u,entry);
    CHECK((uint32_t)r==H&&r>>32==0x33330002u&&r16(0,H+0xb1)==2);
    /* latch_set */
    r=es2_latch_set_registers(&memory,W,entry);
    CHECK((uint32_t)r==W+0x14&&r>>32==entry[1]&&b8(W+0x14)[0]==1);
    /* gauge_state: the copy and its address. */
    w16(0,H+0xb1,0x0102);b8(H)[0xb3]=0x7f;b8(H)[0xb4]=0x80;
    r=es2_gauge_state_registers(&memory,B,H,entry,&ecx);
    CHECK((uint32_t)r==G(0xd1a5c)&&r>>32==0x33330102u&&ecx==0x5555667fu);
    CHECK(r32(0,G(0xd1a5c))==0x807f0102u);
    /* member_toggle: s16 index, the current group's table. */
    b8(W)[0x1c]=1;w32(0,W+0x1f+6,T0);w16(0,T0+6,0x8000);
    r=es2_member_toggle_registers(&memory,W,0x77770003u,&ecx);
    CHECK((uint32_t)r==W&&r>>32==3&&ecx==0x8001u&&r16(0,T0+6)==0x8001);
    w16(0,T0-2,1);r=es2_member_toggle_registers(&memory,W,0x0000ffffu,&ecx);
    CHECK(r>>32==0xffffffffu&&ecx==0xffff0000u&&r16(0,T0-2)==0);
}

static void test_link(void){
    uint32_t ecx;reset();
    w32(0,W+4,LIST);w32(0,LIST+4,E0);w32(0,LIST+12,E1);w32(0,E0+0x18,D0);w32(0,E0+0x1c,C0);w32(0,E1+0x1c,C1);
    /* Nothing selected. */
    b8(W)[0x1d]=0xff;uint64_t r=es2_link_toggle_registers(&memory,W,entry,&ecx);
    CHECK((uint32_t)r==W&&r>>32==0x333344ffu&&ecx==0);
    b8(W)[0x1d]=1;
    /* Definition byte 0. */
    r=es2_link_toggle_registers(&memory,W,entry,&ecx);CHECK((uint32_t)r==W&&r>>32==E0&&ecx==0);
    /* Partner slot 1+2 = 3 holds E1, carriers differ. */
    b8(D0)[0x16]=2;w16(0,C0+0x56,5);w16(0,C1+0x56,6);
    r=es2_link_toggle_registers(&memory,W,entry,&ecx);CHECK((uint32_t)r==E1&&r>>32==E0&&ecx==0);
    w16(0,C1+0x56,5);b8(E1)[0x4b]=1;
    r=es2_link_toggle_registers(&memory,W,entry,&ecx);
    CHECK((uint32_t)r==E1+0x43&&r>>32==E0+0x43&&ecx==1&&b8(E0)[0x4b]==1&&b8(E1)[0x4b]==0);
    /* Empty partner slot (1-1 = 0). */
    b8(D0)[0x16]=0xff;r=es2_link_toggle_registers(&memory,W,entry,&ecx);
    CHECK((uint32_t)r==0&&r>>32==E0&&ecx==0);
}

static void test_chain(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    w32(0,E0,VT);w32(0,VT+0x2c,0x40ecd4u);w32(0,VT+0x60,0x40e62cu);w32(0,W+4,LIST);w32(0,LIST+8,E1);
    /* V2C AX = 0. */
    ret[0][0]=0xffff0000u;ret[0][1]=0x12u;ret[0][2]=0x34u;
    uint64_t r=es2_chain_ready_registers(&memory,&g,&saved,W,E0,1,0,entry,&ecx);
    CHECK(calls==1&&log_[0].f==0x40ecd4u&&log_[0].slot==0x2c&&log_[0].a[0]==E0&&log_[0].r[0]==VT&&log_[0].r[1]==entry[1]);
    CHECK(log_[0].s[0]==W&&log_[0].s[1]==E0&&log_[0].s[2]==1&&(uint32_t)r==0&&r>>32==0x12u&&ecx==E0);
    /* Range set and refused. */
    reset();w32(0,E0,VT);w32(0,VT+0x2c,0x40ecd4u);w32(0,VT+0x60,0x40e62cu);w32(0,W+0x31,77);
    ret[0][0]=1;ret[1][0]=0x10000;ret[1][1]=9;ret[1][2]=8;
    r=es2_chain_ready_registers(&memory,&g,&saved,W,E0,1,0,entry,&ecx);
    CHECK(calls==2&&log_[1].f==B+0xe5f8u&&log_[1].a[1]==77&&log_[1].r[1]==77&&log_[1].r[2]==E0&&(uint32_t)r==0&&ecx==8);
    /* V60 = 2 with an empty table word; then 3 with f = 1. */
    reset();w32(0,E0,VT);w32(0,VT+0x2c,0x40ecd4u);w32(0,VT+0x60,0x40e62cu);
    ret[0][0]=1;ret[1][0]=0xabcd0002u;ret[1][2]=0x99;
    r=es2_chain_ready_registers(&memory,&g,&saved,W,E0,1,0,entry,&ecx);
    CHECK(calls==2&&log_[1].slot==0x60&&log_[1].r[2]==VT&&log_[1].a[1]==0&&(uint32_t)r==0&&ecx==0x99);
    reset();w32(0,E0,VT);w32(0,VT+0x2c,0x40ecd4u);w32(0,VT+0x60,0x40e62cu);w16(0,W+0xa+4,1);
    ret[0][0]=1;ret[1][0]=0xabcd0002u;
    r=es2_chain_ready_registers(&memory,&g,&saved,W,E0,1,0x10000,entry,&ecx);
    /* s16 2: word W+0Eh set; f's low word 0, E+4Bh 0: (E+43h) high | 1. */
    CHECK((uint32_t)r==(((E0+0x43)&0xffff0000u)|1u));
    r=es2_chain_ready_registers(&memory,&g,&saved,W,E0,1,1,entry,&ecx);   /* reuses ret[2..] = 0 */
    CHECK((uint32_t)r==0);
    /* Linked: recursion into the partner slot 1 + 1. */
    reset();w32(0,E0,VT);w32(0,VT+0x2c,0x40ecd4u);w32(0,VT+0x60,0x40e62cu);w32(0,W+4,LIST);w32(0,LIST+8,E1);
    b8(E0)[0x4b]=1;w32(0,E0+0x18,D0);b8(D0)[0x16]=1;
    ret[0][0]=1;ret[1][0]=5;ret[2][0]=0x4242u;ret[2][1]=0x43u;ret[2][2]=0x44u;
    r=es2_chain_ready_registers(&memory,&g,&saved,W,E0,0xffff0001u,0,entry,&ecx);
    CHECK(calls==3&&log_[2].f==B+0x10970u&&log_[2].a[0]==W&&log_[2].a[1]==E1&&log_[2].a[2]==0xffff0002u&&log_[2].a[3]==1);
    CHECK(log_[2].r[0]==1&&log_[2].r[1]==0xffff0002u&&log_[2].r[2]==2&&log_[2].s[1]==E1&&(uint32_t)r==0x4242u&&ecx==0x44u);
}

static void test_ready(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    /* selected_ready: latch W+18h. */
    b8(W)[0x18]=1;uint64_t r=es2_selected_ready_registers(&memory,&g,&saved,W,E0,3,entry,&ecx);
    CHECK(calls==0&&(uint32_t)r==((W&0xffff0000u)|1u)&&r>>32==W+0x14&&ecx==entry[2]);
    b8(W)[0x18]=0;w16(0,G(0xd2526),1);r=es2_selected_ready_registers(&memory,&g,&saved,W,E0,3,entry,&ecx);
    CHECK(calls==0);
    w16(0,G(0xd2526),0);ret[0][0]=7;ret[0][1]=8;ret[0][2]=9;
    r=es2_selected_ready_registers(&memory,&g,&saved,W,E0,0x12340003u,entry,&ecx);
    CHECK(calls==1&&log_[0].f==B+0x10970u&&log_[0].a[2]==0x55550003u&&log_[0].a[3]==0&&log_[0].r[0]==W&&log_[0].r[1]==E0);
    CHECK(log_[0].s[0]==saved.ebx&&(uint32_t)r==7&&r>>32==8&&ecx==9);
    /* ready_step: nothing selected. */
    reset();b8(W)[0x1d]=0x80;r=es2_ready_step_registers(&memory,&g,&saved,W,entry,&ecx);
    CHECK(calls==0&&(uint32_t)r==0x11112280u&&r>>32==0xffffff81u&&ecx==entry[2]);
    /* The selected weapon is usable at once. */
    reset();w32(0,W+4,LIST);w16(0,W+8,2);w32(0,LIST+4,E1);b8(W)[0x1d]=1;ret[0][0]=1;
    r=es2_ready_step_registers(&memory,&g,&saved,W,entry,&ecx);
    CHECK(calls==1&&log_[0].f==B+0x10a04u&&log_[0].a[1]==E1&&log_[0].a[2]==1&&log_[0].r[0]==2&&ecx==0x20000u);
    /* Count 1: three failing steps to slot 0, then the old selection is
     * restored through 410708. */
    reset();w32(0,W+4,LIST);w16(0,W+8,1);w32(0,LIST,E0);w32(0,LIST+4,E1);b8(W)[0x1d]=1;step_count=3;
    r=es2_ready_step_registers(&memory,&g,&saved,W,entry,&ecx);
    CHECK(calls==8&&log_[1].f==B+0x1074cu&&log_[2].a[1]==E0&&log_[2].a[2]==0&&log_[3].f==B+0x1074cu&&log_[7].f==B+0x10708u);
    CHECK(log_[7].a[1]==1&&log_[7].r[0]==1&&log_[7].r[1]==1&&log_[7].r[2]==1&&log_[7].s[1]==E0&&log_[7].s[2]==1);
    CHECK(log_[3].r[0]==0&&log_[3].r[1]==0&&log_[3].r[2]==1&&log_[5].r[0]==1&&ecx==0x10003u&&b8(W)[0x1d]==1);
    /* Found on the second step: W+18h cleared, EAX = W+14h. */
    reset();w32(0,W+4,LIST);w16(0,W+8,2);w32(0,LIST,E0);w32(0,LIST+4,E1);b8(W)[0x1d]=1;b8(W)[0x18]=9;step_count=2;
    ret[4][0]=1;ret[4][1]=0x31;
    r=es2_ready_step_registers(&memory,&g,&saved,W,entry,&ecx);
    CHECK(calls==5&&(uint32_t)r==W+0x14&&r>>32==0x31&&ecx==0x20001u&&b8(W)[0x18]==0);
}

static void test_gauge(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    w32(0,G(0x9b088),P);w32(0,P+0x203,P+0x40);w32(0,P+0x40+0x202,W);
    /* gauge_link */
    uint64_t r=es2_gauge_link_registers(&memory,&g,&saved,H,0x777701u,entry,&ecx);
    CHECK(calls==1&&log_[0].f==B+0x10f14u&&log_[0].a[0]==W&&log_[0].r[0]==W&&log_[0].r[1]==0x33334401u&&log_[0].r[2]==P+0x40);
    CHECK(b8(H)[0xb4]==1&&ecx==W&&log_[0].s[1]==0x5e5e5e5eu);
    /* gauge_sync: all three differ. */
    reset();w16(0,REC,2);b8(REC)[2]=1;b8(REC)[3]=1;
    ret[0][0]=H;ret[0][1]=0xaaaa0002u;ret[1][0]=0x00000000u;ret[1][1]=0x77;ret[1][2]=0x66;
    r=es2_gauge_sync_registers(&memory,&g,&saved,H,REC,entry,&ecx);
    CHECK(calls==3&&log_[0].f==B+0x41f90u&&log_[0].a[1]==0x11110002u&&log_[1].f==B+0x41fa4u&&log_[1].r[1]==0xaaaa0000u);
    CHECK(log_[2].f==B+0x42054u&&log_[2].a[1]==1&&log_[2].r[1]==1&&log_[2].r[2]==H&&log_[2].s[0]==H&&log_[2].s[1]==REC);
    CHECK(r16(0,H+0xb1)==2);
    /* gauge_button: button 0 cycles 2 -> 0. */
    reset();w32(0,H+0x68,4);for(unsigned k=0;k<4;++k)w32(0,H+0x18+4*k,0x1000+k);w16(0,H+0xb1,2);
    r=es2_gauge_button_registers(&memory,&g,&saved,H,0x1000,&ecx);
    CHECK(calls==1&&r16(0,H+0xb1)==0&&log_[0].a[1]==0xffff0000u&&log_[0].r[1]==H+0x18&&log_[0].s[0]==0x1000);
    reset();w32(0,H+0x68,4);for(unsigned k=0;k<4;++k)w32(0,H+0x18+4*k,0x1000+k);
    r=es2_gauge_button_registers(&memory,&g,&saved,H,0x1001,&ecx);CHECK(calls==1&&log_[0].f==B+0x42054u&&log_[0].a[1]==1);
    r=es2_gauge_button_registers(&memory,&g,&saved,H,0x1002,&ecx);CHECK(calls==2&&log_[1].f==B+0x41fa4u&&ecx==H);
    r=es2_gauge_button_registers(&memory,&g,&saved,H,0x1003,&ecx);CHECK(calls==2&&(uint32_t)r==1&&r>>32==H+0x24);
    r=es2_gauge_button_registers(&memory,&g,&saved,H,0x2000,&ecx);CHECK(calls==2&&(uint32_t)r==4&&r>>32==H+0x28&&ecx==H);
}

static void test_frame(void){
    es2_weapon_graph g=graph();uint32_t ecx;reset();
    w32(0,G(0x9b088),P);w32(0,P+0x1e1,H);w32(0,W+4,LIST);w16(0,W+8,3);w32(0,LIST+4,E0);w32(0,LIST+8,E1);
    w32(0,E0,VT);w32(0,E1,VT);w32(0,VT+0x50,0x40f144u);b8(W)[0x1d]=1;w32(0,W+0x1f,T0);w16(0,T0+2,1);
    w16(0,G(0xd2528),1);
    /* 41074c, 410a3c, 442120 (returns REC-like copy), fire_group, 4420c8,
     * then per weapon 410970 and V50. */
    w32(0,OBJ+0xd00,0x00010002u);
    ret[2][0]=OBJ+0xd00;
    uint64_t r=es2_weapon_frame_registers(&memory,&g,&saved,W,0x5151u,REC,entry,&ecx);
    CHECK(calls==10&&log_[0].f==B+0x1074cu&&log_[0].r[0]==entry[0]&&log_[0].s[1]==0x5e5e5e5eu);
    CHECK(log_[1].f==B+0x10a3cu&&log_[1].r[0]==W+0x14&&log_[1].r[1]==0x5151u&&r32(0,W+0x31)==0x5151u&&b8(W)[0x18]==0);
    CHECK(log_[2].f==B+0x42120u&&log_[2].a[0]==H&&log_[2].r[2]==P&&log_[2].s[1]==H);
    CHECK(log_[3].f==B+0x10ae4u&&log_[3].a[1]==(((W+0x14)&0xffff0000u)|2u)&&log_[3].r[2]==H);
    CHECK(log_[4].f==B+0x10f04u&&b8(REC)[3]==0);
    CHECK(log_[5].f==B+0x420c8u&&log_[5].a[1]==REC&&log_[5].r[1]==REC&&log_[5].r[2]==W);
    CHECK(log_[6].f==B+0x10970u&&log_[6].a[1]==E0&&log_[6].a[2]==1&&log_[6].s[1]==1&&log_[6].s[2]==E0);
    CHECK(log_[7].slot==0x50&&log_[7].a[0]==E0&&log_[7].a[1]==1&&log_[7].a[2]==1&&log_[7].r[0]==VT&&log_[7].r[1]==T0);
    CHECK(log_[8].f==B+0x10970u&&log_[8].a[1]==E1&&log_[9].slot==0x50&&log_[9].a[0]==E1&&log_[9].a[1]==0&&log_[9].a[2]==0);
    (void)r;(void)ecx;
}

int main(void){
    test_leaves();test_link();test_chain();test_ready();test_gauge();test_frame();
    printf("weapon_slots: %u checks passed\n",checks);
    return 0;
}
