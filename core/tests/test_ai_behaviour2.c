#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/ai_behaviour2.h"
/* Synthetic guest: objects at 90000000h, frame scratch at A0000000h, image
 * base B with a window 499000h..4d9000h for the tables and globals. The
 * callees are logged with their slots, entry registers and the frame the
 * core names, and return planned values. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define WIN 0x499000u
static uint8_t arena[16384],scratch[0x40],image[0x40000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"ai_behaviour2 line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(WIN)<sizeof(image))return image+(a-G(WIN));
    fprintf(stderr,"ai_behaviour2: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[6],r[3]; unsigned n,slot; es2_ai_call_frame fr; } logged;
static logged log_[64];static unsigned calls,observed;
static uint32_t ret[64][3];
static es2_ai_call_frame frame_;
/* A callee that changes memory: index of the call and an action. */
static unsigned poke_at=99;static uint32_t poke_addr,poke_value;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->fr=frame_;l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(calls==poke_at)w32(0,poke_addr,poke_value);
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){memset(&frame_,0,sizeof(frame_));calls=observed=0;poke_at=99;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));
    memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));}
#define A (OBJ+0x100)
#define T (OBJ+0x600)
#define E (OBJ+0xb00)
#define GRP (OBJ+0x1000)
#define VT (OBJ+0x1100)
#define CFG (OBJ+0x1180)
#define SR (OBJ+0x1200)
#define ORD (OBJ+0x1280)
/* A group with the given members (leader first). */
static void group(uint32_t g,unsigned n,const uint32_t *members){
    w32(0,g+0xc,g+0x100);w16(0,g+0x10,(uint16_t)n);for(unsigned i=0;i<n;++i)w32(0,g+0x100+4*i,members[i]);
}

static void test_aim_select(void){
    es2_weapon_graph g=graph();reset();
    /* Type 0: aim_target with lo(T, w) and the entry EDX/ECX; EDI = the caller's. */
    w32(0,A+0x1a4,T);ret[0][0]=5;ret[0][1]=6;
    uint64_t r=es2_aim_select_registers(&memory,&g,&frame_,0x77,A,0xabcd1234u,0,entry);
    CHECK(calls==1&&log_[0].f==G(0x41e984)&&log_[0].a[0]==A&&log_[0].a[1]==0x1234&&log_[0].a[2]==T&&log_[0].r[1]==entry[1]);
    CHECK(log_[0].fr.ebx==T&&log_[0].fr.esi==A&&log_[0].fr.edi==0x77&&log_[0].fr.ret==G(0x41eaaf)&&r==((uint64_t)6<<32|5));
    /* Type 1: part index from +54h, +58h writes Q, aim_fire with EDI = T. */
    reset();w32(0,T,VT);w16(0,T+0x1a8,1);w32(0,VT+0x54,0x111);w32(0,VT+0x58,0x222);
    ret[0][0]=0xffff8003u;ret[1][1]=0x55550000u;ret[2][0]=9;
    r=es2_aim_select_registers(&memory,&g,&frame_,0x77,A,0x00004321u,T,entry);
    CHECK(calls==3&&log_[0].f==0x111&&log_[0].slot==0x54&&log_[0].r[0]==VT&&log_[1].a[1]==0xffff8003u&&log_[1].a[2]==FRAME);
    CHECK(log_[2].f==G(0x41f5a0)&&log_[2].a[1]==FRAME&&log_[2].a[2]==0x55554321u&&log_[2].r[2]==FRAME&&log_[2].fr.edi==T&&log_[2].fr.ret==G(0x41eb28));
    CHECK((uint32_t)r==9);
    /* Other types: Q = position + the second +30h record. */
    reset();w32(0,T,VT);w16(0,T+0x1a8,2);w32(0,T+0x26,100);w32(0,T+0x2a,200);w32(0,T+0x2e,300);
    w16(0,FRAME+0xc,0xfffe);w16(0,FRAME+0xe,5);w16(0,FRAME+0x10,7);
    r=es2_aim_select_registers(&memory,&g,&frame_,0x77,A,0x1,T,entry);
    CHECK(calls==2&&log_[0].slot==0x30&&log_[0].a[1]==FRAME+0x14&&log_[0].a[2]==FRAME+0xc&&log_[0].fr.ret==G(0x41eb00));
    CHECK(r32(0,FRAME)==98&&r32(0,FRAME+4)==205&&r32(0,FRAME+8)==307&&log_[1].r[0]==5&&log_[1].a[2]==1&&log_[1].fr.edi==0x77);
}

static void test_attack_plan_types(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t mem[]={A};group(GRP,1,mem);w32(0,A+0x45,GRP);w32(0,A,VT);w32(0,VT+0x4c,0x444);
    w8(A+0xac,1);w32(0,A+0x1a4,T);w16(0,T+0x1a8,0);w32(0,A+0x4d,SR);
    /* Leader alone: no member call; retreat 0 twice; +4Ch 2 with byte A+0A8h: 4994da. */
    ret[2][0]=2;w8(A+0xa8,1);ret[3][1]=0x31;ret[4][1]=0x42;
    uint64_t r=es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(r8(A+0xac)==0&&r16(0,A+0x2a2)==0xffff&&calls==5&&log_[0].f==G(0x41cb94)&&log_[0].r[0]==0&&log_[0].r[1]==GRP);
    CHECK(log_[1].f==G(0x41cb94)&&log_[1].r[2]==A&&log_[2].slot==0x4c&&log_[2].a[1]==T&&log_[2].r[2]==VT);
    CHECK(log_[3].f==G(0x41ce08)&&log_[3].r[0]==T&&log_[3].r[1]==A+0x92&&log_[3].r[2]==1);
    CHECK(log_[4].f==G(0x413e50)&&log_[4].a[0]==A+0x4d&&log_[4].a[1]==G(0x4994da)&&log_[4].r[1]==0x31&&log_[4].r[2]==A&&r==(uint64_t)0x42<<32);
    /* +4Ch 2 without the bytes: the configuration word decides (BAh: 49949c). */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w32(0,A,VT);w8(A+0xac,1);w32(0,A+0x1a4,T);w32(0,A+0x1f2,CFG);w16(0,CFG+0xc8,0xba);
    ret[2][0]=0xabcd0002u;
    es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(log_[3].r[1]==CFG&&log_[3].r[2]==0xba&&log_[4].a[1]==G(0x49949c));
    w16(0,CFG+0xc8,0xb9);calls=observed=0;w8(A+0xac,1);
    es2_attack_plan_registers(&memory,&g,A,entry);CHECK(log_[4].a[1]==G(0x4994da));
    /* +4Ch 0 and 1; a building target (1, 3) and a flyer (2) skip it. */
    static const struct { uint16_t type,k; uint32_t table,eax; } cases[]={{0,0,0x4994da,0xffffffffu},{0,1,0x49945e,0},
        {1,0,0x499518,0},{3,0,0x499518,0},{2,0,0x499556,0}};
    for(unsigned i=0;i<5;++i){
        reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w32(0,A,VT);w8(A+0xac,1);w32(0,A+0x1a4,T);w16(0,T+0x1a8,cases[i].type);
        ret[2][0]=cases[i].k;es2_attack_plan_registers(&memory,&g,A,entry);
        const logged *s=log_+calls-1;
        CHECK(s->f==G(0x413e50)&&s->a[1]==G(cases[i].table)&&s->r[0]==G(cases[i].table));
        if(cases[i].type)CHECK(calls==2&&s->r[1]==T&&s->r[2]==A);
        else CHECK(calls==5&&log_[3].r[0]==cases[i].eax);
    }
    /* Retreat: the first check ends the step with its EDX. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w8(A+0xac,1);ret[0][0]=1;ret[0][1]=0x99;
    r=es2_attack_plan_registers(&memory,&g,A,entry);CHECK(calls==1&&r==(uint64_t)0x99<<32);
}

static void test_attack_plan_members(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t E2=OBJ+0x2000,E3=OBJ+0x2800,mem[]={A,E2,E};group(GRP,3,mem);
    w32(0,A+0x45,GRP);w8(A+0xac,1);w32(0,A+0x1a4,T);w16(0,T+0x1a8,2);
    w32(0,E+0x4d,SR);w32(0,E2+0x4d,SR+0x20);w8(SR+0x29,1);   /* E2's script flag: skipped */
    ret[1][0]=7;
    es2_attack_plan_registers(&memory,&g,A,entry);
    /* E only; the count ends the walk with ECX = E (popped). */
    CHECK(log_[0].f==G(0x41d5ec)&&log_[0].a[0]==E&&log_[0].r[0]==E&&log_[0].r[1]==SR+8&&log_[0].r[2]==GRP+0x100);
    CHECK(log_[1].f==G(0x41cb94)&&log_[1].r[0]==0&&log_[1].r[2]==E&&log_[1].r[1]==GRP&&calls==2);
    /* A null entry ends the walk before E3 with ECX = the list. */
    reset();const uint32_t mem4[]={A,E,0,E3};group(GRP,4,mem4);
    w32(0,A+0x45,GRP);w8(A+0xac,1);w32(0,E+0x4d,SR);w32(0,E3+0x4d,SR);ret[1][0]=7;
    es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(calls==2&&log_[0].a[0]==E&&log_[1].r[2]==GRP+0x100);
    /* Not the leader: no walk, EDX = the group, ECX = the list. */
    reset();const uint32_t mem2[]={E,A};group(GRP,2,mem2);w32(0,A+0x45,GRP);w8(A+0xac,1);w32(0,A+0x1a4,T);w16(0,T+0x1a8,2);
    es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x41cb94)&&log_[0].r[2]==GRP+0x100&&calls==2);
}

static void test_attack_plan_orders(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t mem[]={A};
    /* State 0, a type-5 order and the script flag: order_resume at once. */
    group(GRP,1,mem);w32(0,A+0x45,GRP);w32(0,GRP+0x6c,1);w32(0,GRP+0x48,ORD);w16(0,ORD,5);w32(0,A+0x4d,SR);w8(SR+9,0x80);
    ret[0][1]=0x13;
    uint64_t r=es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(calls==1&&log_[0].f==G(0x41eb34)&&log_[0].r[0]==1&&log_[0].r[1]==0xffffff80u&&(log_[0].r[2]&0xff)==0x80&&r==(uint64_t)0x13<<32);
    /* State 2, no order: target_select(A,1,0); the new target replaces the old. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w16(0,A+0x23e,2);w32(0,A+0x4d,SR);w32(0,A+0x1a4,E);w16(0,E+0x1a2,3);
    w16(0,T+0x1a8,2);ret[0][0]=T;ret[0][2]=0x88;
    es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x411fa0)&&log_[0].a[1]==1&&log_[0].r[0]==0&&log_[0].r[1]==0);
    CHECK(r32(0,A+0x1a4)==T&&r16(0,E+0x1a2)==2&&r16(0,T+0x1a2)==1&&r8(A+0x9d)==1&&log_[1].f==G(0x41cb94));
    /* None found: order_resume with the registers of the switch. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w16(0,A+0x23e,1);w32(0,A+0x4d,SR);w32(0,A+0x1a4,E);
    ret[0][0]=0;ret[0][1]=0x5;
    es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(calls==2&&log_[1].f==G(0x41eb34)&&log_[1].r[0]==0&&log_[1].r[2]==E&&log_[1].r[1]==5&&r32(0,A+0x1a4)==0);
    /* State 4: the order target; a destroyed one triggers a new choice. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w16(0,A+0x23e,4);w32(0,A+0x248,T);w8(T+0x99,1);
    ret[0][0]=E;w16(0,E+0x1a8,2);
    es2_attack_plan_registers(&memory,&g,A,entry);
    CHECK(log_[0].f==G(0x411fa0)&&log_[0].a[1]==0&&log_[0].r[0]==T+0x92&&log_[0].r[1]==T&&log_[0].r[2]==T);
    CHECK(r32(0,A+0x1a4)==E&&r16(0,T+0x1a2)==0&&r16(0,E+0x1a2)==1);
    /* The fire permission byte. */
    reset();const uint32_t L=E,mem3[]={L,A};group(GRP,2,mem3);w32(0,A+0x45,GRP);w8(L+0xa3,1);w8(A+0x96,1);w8(A+0xac,1);
    w32(0,A+0x1a4,T);w16(0,T+0x1a8,2);
    es2_attack_plan_registers(&memory,&g,A,entry);CHECK(r8(A+0x96)==0);
    reset();group(GRP,2,mem3);w32(0,A+0x45,GRP);w8(L+0xa3,1);w8(A+0xb2,1);w8(A+0xac,1);w32(0,A+0x1a4,T);w16(0,T+0x1a8,2);
    es2_attack_plan_registers(&memory,&g,A,entry);CHECK(r8(A+0x96)==1);
    reset();group(GRP,2,mem3);w32(0,A+0x45,GRP);w16(0,A+0x26b,1);w8(A+0xac,1);w32(0,A+0x1a4,T);w16(0,T+0x1a8,2);
    es2_attack_plan_registers(&memory,&g,A,entry);CHECK(r8(A+0x96)==0);
}

static void test_attack_replan(void){
    es2_weapon_graph g=graph();reset();ret[0][0]=3;ret[0][1]=4;
    uint64_t r=es2_attack_replan_registers(&memory,&g,A,entry);
    CHECK(calls==1&&log_[0].f==G(0x41cf18)&&log_[0].r[0]==A&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]&&r==((uint64_t)4<<32|3));
}

static void test_scans(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t mem[]={A};
    /* group_scan: drop T, timer expired, not out, a target the order allows. */
    group(GRP,1,mem);w32(0,A+0x45,GRP);w32(0,A+0x1a4,T);w16(0,T+0x1a2,1);w8(A+0xb6,1);
    ret[1][0]=0;ret[2][0]=E;ret[2][2]=0x66;ret[3][0]=1;ret[4][1]=0x12;
    uint64_t r=es2_group_scan_registers(&memory,&g,A,entry);
    CHECK(calls==5&&log_[0].f==G(0x41d598)&&log_[0].r[0]==entry[0]&&log_[1].f==G(0x467a48)&&log_[1].a[0]==A+0x5a);
    CHECK(log_[1].r[0]==A+0x92&&log_[1].r[1]==0&&log_[1].r[2]==T&&r16(0,A+0x5b)==10000&&r16(0,T+0x1a2)==0);
    CHECK(log_[2].f==G(0x411fa0)&&log_[2].a[1]==1&&log_[2].r[2]==A+0x5a&&log_[3].f==G(0x423918)&&log_[3].a[0]==GRP&&log_[3].r[0]==E&&log_[3].r[2]==0x66);
    CHECK(r32(0,A+0x1a4)==E&&r8(A+0xac)==1&&log_[4].f==G(0x41d5ec)&&log_[4].r[0]==A+0x92&&log_[4].r[1]==A+0x92&&r==(uint64_t)0x12<<32);
    /* Not the leader and not watching: nothing after group_move. */
    reset();const uint32_t mem2[]={E,A};group(GRP,2,mem2);w32(0,A+0x45,GRP);
    r=es2_group_scan_registers(&memory,&g,A,entry);CHECK(calls==1&&r==(uint64_t)GRP<<32);
    /* Out: retreat script. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w8(A+0x99,1);ret[2][0]=T;
    es2_group_scan_registers(&memory,&g,A,entry);
    CHECK(log_[2].a[1]==0&&log_[2].r[0]==1&&log_[2].r[1]==1&&log_[3].f==G(0x413e50)&&log_[3].a[1]==G(0x499800)&&r16(0,A+0x2a2)==0xffff);
    /* Timer running: EDX = 0. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);ret[0][1]=0x9;ret[1][0]=1;
    r=es2_group_scan_registers(&memory,&g,A,entry);CHECK(calls==2&&r==0);
    /* order_scan: the state word is read before group_move. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w16(0,A+0x23e,2);w32(0,A+0x1a4,T);w16(0,T+0x1a2,1);
    poke_at=0;poke_addr=A+0x23e;poke_value=0;ret[2][0]=E;
    es2_order_scan_registers(&memory,&g,A,entry);
    CHECK(calls==4&&log_[1].r[1]==T&&log_[1].r[2]==0&&log_[2].f==G(0x411fa0)&&log_[2].r[0]==0&&log_[3].f==G(0x41d5ec)&&r32(0,A+0x1a4)==E);
    /* State 0 under a type-3 order. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w32(0,GRP+0x6c,0);w32(0,GRP+0x44,ORD);w16(0,ORD,3);ret[2][0]=0;
    r=es2_order_scan_registers(&memory,&g,A,entry);
    CHECK(calls==3&&log_[2].r[0]==((ORD&0xffff0000u)|3)&&log_[2].r[1]==0&&log_[2].r[2]==0);
    /* State 0 under another order: no choice, EDX = the order index. */
    reset();group(GRP,1,mem);w32(0,A+0x45,GRP);w32(0,GRP+0x6c,2);w32(0,GRP+0x4c,ORD);w16(0,ORD,4);
    r=es2_order_scan_registers(&memory,&g,A,entry);CHECK(calls==2&&r==(uint64_t)2<<32);
}

static void test_retreat_fire(void){
    es2_weapon_graph g=graph();reset();
    const uint32_t S=FRAME;
    w32(0,A+0x1a4,T);w16(0,T+0x1a2,1);ret[0][2]=0x31;
    /* target_bearing writes S: magnitude 3000h (not below), aspect word 0x4444. */
    w16(0,S+4,0x3000);w16(0,S+6,0x4444);w16(0,S+2,0x1000);
    ret[1][0]=0;               /* timer expired */
    ret[2][0]=0x77770001u;     /* rng: side 1 */
    ret[3][0]=0x12340000u;     /* drive_apply */
    w8(T+0xa4,1);
    uint64_t r=es2_retreat_fire_registers(&memory,&g,&frame_,0x5a5a,A,entry);
    CHECK(r32(0,A+0x61)==T&&r32(0,A+0x1a4)==0&&r16(0,T+0x1a2)==0&&r8(A+0x9d)==1);
    CHECK(calls==5&&log_[0].f==G(0x41e758)&&log_[0].a[1]==S&&log_[0].a[2]==T&&log_[0].r[0]==T&&log_[0].r[2]==T);
    CHECK(log_[1].f==G(0x467aa8)&&log_[1].a[0]==A+0x5a&&log_[2].f==G(0x492ee0)&&log_[2].a[0]==G(0x4d2599)&&log_[2].r[2]==1);
    CHECK(r32(0,A+0x5b)==0xfa0&&r16(0,A+0x5f)==1&&r16(0,S+0xe)==1&&r16(0,S+2)==0xb000&&r32(0,S+0x14)==10000000u);
    CHECK(log_[3].f==G(0x41e828)&&log_[3].r[2]==G(0x4d2599)&&log_[4].f==G(0x41ea7c)&&log_[4].a[1]==0x12344444u&&log_[4].a[2]==T&&log_[4].r[2]==T);
    CHECK(log_[4].fr.ebx==A&&log_[4].fr.esi==A+0x5a&&log_[4].fr.edi==0x5a5a&&log_[4].fr.ret==G(0x41d3c6)&&r==((uint64_t)1<<32|1));
    /* Close (magnitude below 3000h): reverse; side 0 adds C000h over A000h. */
    reset();w32(0,A+0x61,T);w16(0,S+4,0x2fff);w16(0,S+2,0x1000);w32(0,A+0x317,E);
    r=es2_retreat_fire_registers(&memory,&g,NULL,0,A,entry);
    CHECK(calls==4&&r16(0,S+0xe)==0xffff&&r16(0,S+2)==0x9000&&log_[1].f==G(0x40f09c)&&log_[1].r[0]==E&&log_[1].r[1]==0);
    CHECK(log_[2].r[2]==E&&log_[0].r[2]==entry[2]&&r==0);
    reset();w32(0,A+0x61,T);w16(0,S+4,0x4000);w16(0,S+2,0x1000);ret[1][0]=5;
    es2_retreat_fire_registers(&memory,&g,NULL,0,A,entry);
    CHECK(r16(0,S+2)==0x7000&&log_[2].r[2]==0);   /* running: ECX = 0, side word 0: 6000h */
    /* The out test reads V again after aim_select; a5 keeps V's high word. */
    reset();w32(0,A+0x61,T);w16(0,S+4,0x2000);poke_at=2;poke_addr=A+0x61;poke_value=E;w8(E+0xa5,1);
    r=es2_retreat_fire_registers(&memory,&g,NULL,0,A,entry);CHECK(r==((uint64_t)1<<32|((E&0xffff0000u)|1)));
}

int main(void){
    test_aim_select();test_attack_plan_types();test_attack_plan_members();test_attack_plan_orders();
    test_attack_replan();test_scans();test_retreat_fire();
    printf("ai_behaviour2: %u checks passed\n",checks);
    return 0;
}
