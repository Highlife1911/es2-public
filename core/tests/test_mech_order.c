#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mech_order.h"
/* Synthetic guest for the MECH order handler and the wingman order
 *. Objects live in an arena at 90000000h, the globals in a window
 * of an image at B. Every bridged callee is a recording stand-in: 411CB4
 * returns 7, target_select returns the word at the vehicle's +2F0h,
 * dist_gate and dist2d_point return the x distance of the two points, the
 * list step 471C70 walks a fixed array, 4238A0 returns the group leader,
 * 420A98 records the answer code; the wingman virtual +28h calls the core
 * again (like a replaced hook). Expected values are worked out by hand from
 * docs/reverse-engineering/mech_order.md. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[0x4000],globals[0x50000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mech_order line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x490000)<sizeof(globals))return globals+(a-G(0x490000));
    fprintf(stderr,"mech_order: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
/* Layout: cockpit H, group, script record P (A+4Dh points 8 below it),
 * vtable, order record R, the list array, actors (A0 the vehicle, A1 the
 * player, A2..A5 others). */
enum { H=0x000, GRP=0x300, SCRIPT=0x380, VT=0x400, REC=0x480, LIST=0x500, ACT=0x800 };
#define AT(o) (OBJ+(uint32_t)(o))
#define ACTOR(i) AT(ACT+0x300*(i))
#define VEH ACTOR(0)
static const es2_weapon_graph *graph;
static unsigned answers,classes,attacks,resumes,scripts,list_steps,wingman_calls,cancels;
static uint32_t answer_code,attack_target,script_table,list[8],last_regs[3];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;(void)n;uint32_t entry[3]={r->eax,r->edx,r->ecx};uint64_t p=0;
    memcpy(last_regs,entry,sizeof(last_regs));
    if(f==G(0x411cb4)){++classes;p=pair(7,0x11);}
    else if(f==G(0x41c0f4)){++attacks;attack_target=a[1];w32(0,a[0]+0x1a4,a[1]);p=pair(0x41c0,0x41c1);}
    else if(f==G(0x41eb34)){++resumes;p=pair(0x41eb,0x41ec);}
    else if(f==G(0x411fa0)){CHECK(a[1]==1&&a[2]==0);p=pair(r32(0,a[0]+0x2f0),0x1fa0);}
    else if(f==G(0x49288c)||f==G(0x4928d0)){int32_t d=(int32_t)(r32(0,a[1])-r32(0,a[0]));p=pair((uint32_t)(d<0?-d:d),0x288c);}
    else if(f==G(0x471c70)){
        ++list_steps;CHECK(a[0]==r32(0,G(0x4a9b7a)));uint32_t next=0;
        if(!a[1])next=list[0];else for(unsigned i=0;i<7;++i)if(list[i]==a[1]){next=list[i+1];break;}
        p=pair(next,0x1c70);}
    else if(f==G(0x4238a0))p=pair(r32(0,r32(0,a[0]+0xc)),0x38a0);
    else if(f==G(0x413e50)){++scripts;script_table=a[1];w32(0,a[0],a[1]);p=pair(0x3e50,0x3e51);}
    else if(f==G(0x420a98)){++answers;answer_code=a[1];CHECK(a[2]==0);p=pair(0xa98,0xa99);}
    else if(f==r32(0,AT(VT+0x28))){++wingman_calls;p=es2_mech_order_registers(&memory,graph,a[0],a[1],a[2],entry);}
    else if(f==G(0x435ac8)){++cancels;CHECK(a[2]==0x22);p=pair(0x5ac8,0x5ac9);}
    else {fprintf(stderr,"mech_order: unexpected call %08x\n",f);exit(1);}
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);r->ecx=0x5eed0000u;
}
static void actor(unsigned i,int32_t x){
    uint32_t A=ACTOR(i);w32(0,A,AT(VT));w32(0,A+0x26,(uint32_t)x);
    w32(0,A+0x45,AT(GRP));w32(0,A+0x4d,AT(SCRIPT)-8);
}
static void reset(void){
    memset(arena,0,sizeof(arena));memset(globals,0,sizeof(globals));memset(list,0,sizeof(list));
    answers=classes=attacks=resumes=scripts=list_steps=wingman_calls=cancels=0;answer_code=attack_target=script_table=0;
    w32(0,AT(VT+0x28),0xdead0028u);
    for(unsigned i=0;i<6;++i)actor(i,1000*(int32_t)i);
    w32(0,G(0x49b088),AT(H));w32(0,AT(H+0x203),ACTOR(1));w32(0,AT(H+0x207),AT(H+0x280));
    w32(0,G(0x4a9b84),ACTOR(1));w32(0,G(0x4a9b7a),AT(LIST));
    /* Group: leader A1 at +0Ch, order slot index 0, no order record. */
    w32(0,AT(GRP+0xc),AT(GRP+0x40));w32(0,AT(GRP+0x40),ACTOR(1));
}
static uint64_t order(uint16_t code,uint16_t mode){
    const uint32_t entry[3]={0x1111,0x2222,0x3333};w16(0,AT(REC),code);
    return es2_mech_order_registers(&memory,graph,VEH,AT(REC),mode,entry);
}
#define STATE r16(0,VEH+0x23e)
static void attack_checks(void){
    /* 0: attack the cockpit target A3. */
    reset();w32(0,AT(H+0x210),ACTOR(3));
    CHECK(order(0,0)==pair(1,0xa99));
    CHECK(STATE==4&&r32(0,VEH+0x248)==ACTOR(3)&&r16(0,VEH+0x250)==7&&attack_target==ACTOR(3));
    /* The answer code: EDI = target with low word 11h. */
    CHECK(answers==1&&answer_code==((ACTOR(3)&0xffff0000u)|0x11u));
    /* Again: already attacking it, answer 0Fh, EDX from 420A98. */
    CHECK(order(0,0)==pair(0,0xa99)&&(uint16_t)answer_code==0xf&&attacks==1);
    /* Mode 1 answers only taken orders; mode 2 never. */
    answers=0;CHECK(order(0,1)==pair(0,0)&&answers==0);
    reset();w32(0,AT(H+0x210),ACTOR(3));CHECK(order(0,2)==pair(1,0x41c1)&&answers==0);
    /* Disabled vehicle: 1Bh, EAX 0, EDX = 1 (A4h) or unchanged code 0 (A5h). */
    reset();w32(0,AT(H+0x210),ACTOR(3));w8(VEH+0xa4,1);
    CHECK(order(0,2)==pair(0,1));w8(VEH+0xa4,0);w8(VEH+0xa5,1);CHECK(order(0,2)==pair(0,0));
    CHECK(order(0,1)==pair(0,0)&&answers==0);CHECK(order(0,0)==pair(0,0xa99)&&answer_code==((ACTOR(3)&0xffff0000u)|0x1bu));
    /* No cockpit target: 12h exactly (EDI was 0). */
    reset();CHECK(order(0,0)==pair(0,0xa99)&&answer_code==0x12);
    /* Script flag P+4: 0Dh. Dead target: 12h with its high word. */
    reset();w32(0,AT(H+0x210),ACTOR(3));w8(AT(SCRIPT+4),1);order(0,0);CHECK((uint16_t)answer_code==0xd);
    reset();w32(0,AT(H+0x210),ACTOR(3));w8(ACTOR(3)+0x99,1);order(0,0);CHECK(answer_code==((ACTOR(3)&0xffff0000u)|0x12u)&&attacks==0);
    /* 0Bh: attack R+12h. */
    reset();w32(0,AT(REC+0x12),ACTOR(4));CHECK(order(0xb,0)==pair(1,0xa99));
    CHECK(STATE==4&&r32(0,VEH+0x248)==ACTOR(4)&&attack_target==ACTOR(4)&&answer_code==0xffff0011u);
    CHECK(order(0xb,0)==pair(0,0xa99)&&answer_code==0xffff000fu);
    reset();w32(0,AT(REC+0x12),ACTOR(4));w8(ACTOR(4)+0xa4,1);CHECK(order(0xb,0)==pair(0,0xa99)&&answer_code==0xffff001du);
}
static void other_checks(void){
    /* 1: break off; the cockpit target is A's target A3 and the script
     * allows engaging (P+1): target_select gives A5 -> 41C0F4. */
    reset();w32(0,AT(H+0x210),ACTOR(3));w32(0,VEH+0x1a4,ACTOR(3));w16(0,VEH+0x23e,4);w32(0,VEH+0x248,ACTOR(3));
    w8(AT(SCRIPT+1),1);w32(0,VEH+0x2f0,ACTOR(5));
    CHECK(order(1,0)==pair(1,0xa99)&&STATE==0&&r8(VEH+0x9a)==1&&attack_target==ACTOR(5)&&answer_code==0xffff0016u);
    CHECK(order(1,0)==pair(0,0xa99)&&answer_code==0xffff000fu);
    /* target_select 0: 41EB34. P+2 instead: clear A+52h, EDX 0. */
    reset();w32(0,AT(H+0x210),ACTOR(3));w32(0,VEH+0x1a4,ACTOR(3));w8(AT(SCRIPT+1),1);
    CHECK(order(1,2)==pair(1,0x41ec)&&resumes==1&&attacks==0);
    reset();w32(0,AT(H+0x210),ACTOR(3));w32(0,VEH+0x1a4,ACTOR(3));w8(AT(SCRIPT+1),1);w8(AT(SCRIPT+2),1);w32(0,VEH+0x52,9);
    CHECK(order(1,2)==pair(1,0)&&r32(0,VEH+0x52)==0);
    /* 2: the nearest attacker of the player: A3 (x 3000) and A4 (4000)
     * target A1 (x 1000); A2 does not, A5 is dead. */
    reset();list[0]=ACTOR(5);list[1]=ACTOR(4);list[2]=ACTOR(2);list[3]=ACTOR(3);
    w32(0,ACTOR(3)+0x1a4,ACTOR(1));w32(0,ACTOR(4)+0x1a4,ACTOR(1));w32(0,ACTOR(5)+0x1a4,ACTOR(1));w8(ACTOR(5)+0xa4,1);
    CHECK(order(2,0)==pair(1,0xa99)&&list_steps==5&&attack_target==ACTOR(3)&&answer_code==0xb&&STATE==4);
    /* None: 1Dh exactly. Already its target with P+1: 1Ah. */
    reset();list[0]=ACTOR(2);CHECK(order(2,0)==pair(0,0xa99)&&answer_code==0x1d);
    reset();list[0]=ACTOR(3);w32(0,ACTOR(3)+0x1a4,ACTOR(1));w32(0,VEH+0x1a4,ACTOR(3));w8(AT(SCRIPT+1),1);
    CHECK(order(2,0)==pair(0,0xa99)&&answer_code==0x1a);
    /* 4/10h hold, 7/11h release. */
    reset();CHECK(order(4,2)==pair(1,VEH+0x92)&&r8(VEH+0x96)==1&&r8(VEH+0xb2)==1);
    CHECK(order(0x10,0)==pair(1,0xa99)&&answer_code==0xffff000fu);
    CHECK(order(0x11,0)==pair(1,0xa99)&&answer_code==0xffff0028u&&r8(VEH+0x96)==0&&r8(VEH+0xb2)==0);
    /* 5: engage at will; the group order is type 2 with leader A1 at
     * 1000 < 100000: 41EB34 and state 0. */
    reset();w32(0,VEH+0x2f0,ACTOR(4));w16(0,VEH+0x23e,6);
    w32(0,AT(GRP+0x44),AT(GRP+0x60));w16(0,AT(GRP+0x60),2);
    CHECK(order(5,2)==pair(1,VEH+0x92)&&resumes==1&&attacks==0&&STATE==0&&r8(VEH+0xb6)==1);
    w16(0,AT(GRP+0x60),1);w16(0,VEH+0x23e,6);CHECK(order(5,0)==pair(1,0xa99)&&attacks==1&&STATE==0&&answer_code==0xffff001cu);
    reset();w8(AT(SCRIPT+1),1);CHECK(order(5,0)==pair(1,0xa99)&&answer_code==0xffff0020u&&r8(VEH+0xb6)==1);
    /* 8: stop. */
    reset();w8(AT(SCRIPT+1),1);w32(0,VEH+0x281,5);w8(VEH+0xb6,1);
    CHECK(order(8,2)==pair(1,VEH+0x92)&&resumes==1&&r32(0,VEH+0x281)==0&&r8(VEH+0xb6)==0);
    /* 0Eh: move (state 1), then close to the point: 20h, not taken. */
    reset();w32(0,AT(REC+6),1500);w32(0,AT(REC+10),77);
    CHECK(order(0xe,0)==pair(1,0xa99)&&STATE==1&&scripts==1&&script_table==G(0x499594)&&answer_code==0xffff002au);
    CHECK(r32(0,VEH+0x240)==1500&&r32(0,VEH+0x244)==77);
    CHECK(order(0xe,2)==pair(0,AT(REC+6))&&scripts==1);
    w32(0,AT(REC+6),9000);CHECK(order(0xd,0)==pair(1,0xa99)&&STATE==2&&scripts==2);
    w8(VEH+0xa4,1);CHECK(order(0xd,0)==pair(0,0xa99)&&answer_code==0xffff001bu);
    /* 0Ch: guard the point; again within 1000: 20h. */
    reset();w32(0,AT(REC+6),500);
    CHECK(order(0xc,0)==pair(1,0xa99)&&STATE==6&&script_table==G(0x499746)&&answer_code==0xffff0017u);
    CHECK(order(0xc,2)==pair(0,AT(REC+6))&&scripts==1);
    w32(0,AT(REC+0x12),ACTOR(2));CHECK(order(0xc,2)==pair(1,AT(REC+6))&&r32(0,VEH+0x24c)==ACTOR(2));
    CHECK(order(0xc,2)==pair(0,AT(REC+6))&&scripts==2);
    /* 3/0Ah/0Fh: rejoin; leader A1 at 1000 < 25000: 1Eh. */
    reset();w16(0,VEH+0x23e,4);CHECK(order(3,0)==pair(1,0xa99)&&STATE==0&&answer_code==0xffff001eu);
    w32(0,ACTOR(1)+0x26,90000);CHECK(order(0xf,0)==pair(1,0xa99)&&answer_code==0xffff0014u);
    /* 6, 9 and 12h: nothing; mode 0 still answers FFFFFFFFh. */
    reset();CHECK(order(6,2)==pair(0,6)&&order(0x12,2)==pair(0,0x12)&&answers==0);
    CHECK(order(9,0)==pair(0,0xa99)&&answer_code==0xffffffffu);
}
static void wingman_checks(void){
    /* Slot 2 holds A0: R+2 <- player, the order runs, then 435AC8. */
    reset();w32(0,G(0x4d03c8)+8,VEH);w32(0,AT(H+0x210),ACTOR(3));w16(0,AT(REC),0);
    const uint32_t entry[3]={1,2,3};
    CHECK(es2_wingman_order_registers(&memory,graph,AT(H),AT(REC),2,entry)==pair(0x5ac8,0x5ac9));
    CHECK(wingman_calls==1&&cancels==1&&r32(0,AT(REC+2))==ACTOR(1)&&STATE==4);
    CHECK(last_regs[0]==AT(H+0x280));
    /* The player itself, a destroyed wingman or an empty slot: only 435AC8. */
    w32(0,G(0x4d03c8)+8,ACTOR(1));es2_wingman_order_registers(&memory,graph,AT(H),AT(REC),2,entry);
    w32(0,G(0x4d03c8)+8,ACTOR(2));w8(ACTOR(2)+0x99,1);es2_wingman_order_registers(&memory,graph,AT(H),AT(REC),2,entry);
    es2_wingman_order_registers(&memory,graph,AT(H),AT(REC),0xffff,entry);
    CHECK(wingman_calls==1&&cancels==4);
}
int main(void){
    es2_weapon_graph g={B,0,0,0,0,call,NULL,NULL,NULL};graph=&g;
    attack_checks();other_checks();wingman_checks();
    printf("mech_order core: %u checks passed\n",checks);
    return 0;
}
