#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/cockpit_sync.h"
/* Synthetic guest: the vehicles, the cockpit and its records at 90000000h,
 * a stack page at A0000000h (frame EBP = STACK+80h, the outgoing slots
 * below it), the image globals at B. Calls are logged with their target,
 * slot, arguments, entry registers, the frame the core names and the
 * record a pointer argument shows; they return planned registers and
 * apply the effects a test asks for. */
#define OBJ UINT32_C(0x90000000)
#define STACK UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define P (OBJ+0x100)
#define H (OBJ+0x800)
#define POOLG (OBJ+0xa00)
#define TG (OBJ+0xb00)
#define HUD (OBJ+0xc00)
#define W (OBJ+0xd00)
#define C (OBJ+0xd80)
#define T1 (OBJ+0xe00)
#define T2 (OBJ+0x1000)
#define PW (OBJ+0x1200)
#define S (OBJ+0x1300)
#define K (OBJ+0x1380)
#define EBP_ (STACK+0x80)
static uint8_t arena[0x1400],stack[0x100],globals[0x300000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"cockpit_sync line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-STACK<sizeof(stack))return stack+(a-STACK);
    if(a-B<sizeof(globals))return globals+(a-B);
    fprintf(stderr,"cockpit_sync: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[4],r[3],fr[4]; uint8_t rec[0x28]; unsigned n,slot; } logged;
static logged log_[24];static unsigned calls,observed;
static es2_cockpit_sync *active;
static uint32_t slot_after,timer_ax,record_at;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<24);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(active){l->fr[0]=active->call.ebx;l->fr[1]=active->call.esi;l->fr[2]=active->call.edi;l->fr[3]=active->call.ret;}
    if(active&&active->top&&n==1)w32(0,active->top-4,slot_after+calls);   /* the callee changes its slot */
    if(f==G(0x447da8)||f==G(0x43d9b4))for(unsigned i=0;i<0x28;++i)l->rec[i]=*(uint8_t *)host(0,a[1]+i);
    r->eax=0x1000u+calls;r->edx=0x2000u+calls;r->ecx=0x3000u+calls;
    if(f==G(0x447df8)||f==G(0x43dbb0))r->eax=a[0]+0xb1u+record_at;
    if(f==G(0x467a48))r->eax=(r->eax&0xffff0000u)|timer_ax;
    if(f==G(0x41b728)){w32(0,a[1],0x11223344u);w32(0,a[1]+4,0x55667788u);w32(0,a[1]+8,0x99aabbccu);w16(0,a[2],0x0102u);}
    ++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,STACK+0x40,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(log_,0,sizeof(log_));}
static uint32_t rec32(const logged *l,unsigned off){
    return (uint32_t)l->rec[off]|(uint32_t)l->rec[off+1]<<8|(uint32_t)l->rec[off+2]<<16|(uint32_t)l->rec[off+3]<<24;
}
static uint16_t rec16(const logged *l,unsigned off){return (uint16_t)(l->rec[off]|(uint16_t)l->rec[off+1]<<8);}
static void build(void){
    memset(arena,0,sizeof(arena));memset(stack,0xcd,sizeof(stack));
    w32(0,G(0x49b088),H);w32(0,G(0x49ef34),0);w32(0,G(0x4d24e6),P);w32(0,G(0x4d2684),T2);w16(0,G(0x4d2504),0);
    w32(0,H+0x1e5,POOLG);w32(0,H+0x1f1,TG);w32(0,H+0x1f5,HUD);w32(0,H+0x210,0);
    w16(0,P+0x292,3000);w16(0,P+0x28e,200);w16(0,P+0x290,0x155);w16(0,P+0x10,0x1234);w16(0,P+0x298,0x0567);w16(0,P+0x29a,0x0789);
    w32(0,P+0x1f2,C);w32(0,P+0x202,W);w32(0,P+0x1a4,0);w32(0,P+0x30b,0);
    w16(0,C+4,500);w16(0,C+6,800);w16(0,C+0x50,0);
    w32(0,TG+0xb1,0x77);w32(0,TG+0xb5,0x300);w32(0,TG+0xd1,0x66);w32(0,TG+0xd5,0x2a5);
    for(unsigned i=0;i<0x50;++i)arena[HUD+0xb1-OBJ+i]=(uint8_t)(0x40+i);
    w32(0,TG+0x18,S);w32(0,S+0x17,K);w32(0,K+0x14,0xc0000014u);
}
int main(void){
    es2_weapon_graph g=graph();uint64_t v;
    /* 1. No target, the input set the set point (P+93h), W+14h set and the
     *    countdown at 0: plain bridge, nine calls. */
    build();arena[P-OBJ+0x93]=1;arena[W-OBJ+0x14]=1;timer_ax=0;record_at=0;reset();
    v=es2_cockpit_sync_registers(&memory,&g,NULL,0x77,0x88,P,entry);
    CHECK(calls==9&&observed==9);
    CHECK(r32(0,G(0x4a9ce8))==(uint32_t)(3000*1024/10000));
    CHECK(log_[0].f==G(0x4450f8)&&log_[0].n==2&&log_[0].a[0]==POOLG&&log_[0].a[1]==G(0x4a9ce8)&&log_[0].r[0]==H&&log_[0].r[1]==POOLG&&log_[0].r[2]==10000);
    CHECK(log_[1].f==G(0x413bc8)&&log_[1].a[0]==P+0x222&&log_[1].r[0]==0x1000&&log_[1].r[1]==0x2000&&log_[1].r[2]==P+0x222);
    CHECK(log_[2].f==G(0x410b40)&&log_[2].a[0]==W&&log_[2].a[1]==0&&log_[2].r[0]==0&&log_[2].r[1]==W&&log_[2].r[2]==P+0x222);
    CHECK(log_[3].f==G(0x447df8)&&log_[3].a[0]==TG&&log_[3].r[0]==TG&&log_[3].r[1]==0x2002&&log_[3].r[2]==H);
    /* The input's set point goes to the gauge; P+290h is not written. */
    CHECK(arena[P-OBJ+0x93]==0&&r16(0,P+0x290)==0x155);
    CHECK(log_[4].f==G(0x447da8)&&log_[4].a[0]==TG&&log_[4].a[1]==EBP_-8&&log_[4].r[0]==H&&log_[4].r[1]==TG&&log_[4].r[2]==800);
    CHECK(rec32(log_+4,0)==(uint32_t)(200*1024/800)&&rec32(log_+4,4)==0x155);
    CHECK(log_[5].f==G(0x43dbb0)&&log_[5].a[0]==HUD&&log_[5].r[0]==HUD&&log_[5].r[1]==0x2004&&log_[5].r[2]==H);
    CHECK(log_[6].f==G(0x467a48)&&log_[6].a[0]==P+0x31b&&log_[6].r[0]==W+0x14&&log_[6].r[1]==P+0x31b);
    CHECK(r16(0,G(0x4d2504))==1);
    /* The HUD record: heading, twist, pitch from P, target 0, P+9Bh = 0. */
    CHECK(log_[7].f==G(0x43d9b4)&&log_[7].a[0]==HUD&&log_[7].a[1]==EBP_-0x34&&log_[7].r[0]==H&&log_[7].r[1]==HUD&&log_[7].r[2]==EBP_-0x34);
    CHECK(rec16(log_+7,0)==0x1234&&rec16(log_+7,2)==0x0567&&rec16(log_+7,4)==0x0789&&rec32(log_+7,6)==0);
    CHECK(rec32(log_+7,0xa)==r32(0,HUD+0xb1+0xa)&&rec32(log_+7,0xe)==r32(0,HUD+0xb1+0xe)&&rec32(log_+7,0x12)==r32(0,HUD+0xb1+0x12));
    CHECK(rec16(log_+7,0x16)==r16(0,HUD+0xb1+0x16)&&rec32(log_+7,0x1c)==r32(0,HUD+0xb1+0x1c)&&rec32(log_+7,0x20)==r32(0,HUD+0xb1+0x20));
    CHECK(log_[7].rec[0x24]==0x64&&log_[7].rec[0x25]==0&&log_[7].fr[0]==0);   /* plain bridge: no frame stored */
    CHECK(log_[8].f==G(0x434a24)&&log_[8].a[0]==0&&log_[8].a[1]==EBP_-0x40);
    /* AX of the countdown nonzero: the centering word stays. */
    build();arena[P-OBJ+0x93]=1;arena[W-OBJ+0x14]=1;timer_ax=5;reset();
    v=es2_cockpit_sync_registers(&memory,&g,NULL,0x77,0x88,P,entry);
    CHECK(calls==9&&log_[6].f==G(0x467a48)&&r16(0,G(0x4d2504))==0&&log_[7].f==G(0x43d9b4));
    /* W+14h clear: no countdown; 434a24 gets the residue words (CDh),
     *    the high halves of 43d9b4's EAX/ECX and target 0. */
    build();reset();
    v=es2_cockpit_sync_registers(&memory,&g,NULL,0x77,0x88,P,entry);
    CHECK(calls==8&&log_[6].f==G(0x43d9b4)&&log_[7].f==G(0x434a24)&&r16(0,P+0x290)==0x300);
    CHECK(log_[7].a[0]==0&&log_[7].a[1]==EBP_-0x40&&log_[7].a[2]==0x0000cdcdu&&log_[7].a[3]==0x0000cdcdu);
    CHECK(log_[7].r[0]==log_[7].a[2]&&log_[7].r[1]==EBP_-0x40&&log_[7].r[2]==0);
    CHECK(v==((uint64_t)0x2007u<<32|0x1007u));
    /* The gauge's set point comes back into P+290h and P+2D7h on a
     *    flyer chassis; a negative speed divides by the negated reverse
     *    limit. */
    build();w16(0,C+0x50,1);w16(0,P+0x28e,(uint16_t)-300);reset();
    v=es2_cockpit_sync_registers(&memory,&g,NULL,0x77,0x88,P,entry);
    CHECK(r16(0,P+0x290)==0x300&&r16(0,P+0x2d7)==0x300);
    CHECK(log_[4].f==G(0x447da8)&&log_[4].r[2]==(uint32_t)-500&&(int32_t)rec32(log_+4,0)==(-300*1024)/-500&&rec32(log_+4,4)==0x300);
    /* 2. Target unchanged, seen from the view subject: dist_gate from
     *    [4D2684h]+26h, ESI = T+26h at the weapon system; with the shim's
     *    frame the core names the registers and re-reads the slots. */
    build();w32(0,P+0x1a4,T1);w32(0,H+0x210,T1);w32(0,G(0x49ef34),1);arena[P-OBJ+0xa4]=1;reset();
    es2_cockpit_sync fr={{0,0,0,0},EBP_-0x48};active=&fr;slot_after=0x5a000000u;
    v=es2_cockpit_sync_registers(&memory,&g,&fr,0x77,0x88,P,entry);active=NULL;
    CHECK(calls==10);
    CHECK(log_[0].fr[0]==P&&log_[0].fr[1]==0x77&&log_[0].fr[2]==0x88&&log_[0].fr[3]==G(0x41b168));
    CHECK(log_[1].fr[3]==G(0x41b177));
    CHECK(log_[2].f==G(0x49288c)&&log_[2].a[0]==T2+0x26&&log_[2].a[1]==T1+0x26&&log_[2].r[0]==T2+0x26&&log_[2].r[1]==0x2001&&log_[2].r[2]==slot_after+1);
    CHECK(log_[2].fr[1]==T1+0x26&&log_[2].fr[3]==G(0x41b1ac));
    CHECK(log_[3].f==G(0x410b40)&&log_[3].a[1]==0x1002&&log_[3].r[0]==0x1002&&log_[3].r[2]==0x3002&&log_[3].fr[1]==T1+0x26&&log_[3].fr[3]==G(0x41b1d4));
    CHECK(log_[4].fr[3]==G(0x41b1e9)&&log_[5].fr[3]==G(0x41b285)&&log_[6].fr[3]==G(0x41b29a)&&log_[6].fr[2]==0x88);
    /* P+0A4h set: the record keeps the HUD's own heading and twist. */
    CHECK(log_[7].f==G(0x43d9b4)&&rec16(log_+7,0)==r16(0,HUD+0xb1)&&rec16(log_+7,2)==r16(0,HUD+0xb3)&&rec16(log_+7,4)==0x0789);
    CHECK(rec32(log_+7,6)==T1&&log_[7].fr[1]==T1&&log_[7].fr[3]==G(0x41b422));
    CHECK(log_[8].f==G(0x41b728)&&log_[8].n==4&&log_[8].a[0]==P&&log_[8].a[1]==EBP_-0x40&&log_[8].a[2]==EBP_-0xa&&log_[8].a[3]==EBP_-0xc);
    CHECK(log_[8].r[0]==EBP_-0xa&&log_[8].r[1]==EBP_-0x40&&log_[8].r[2]==EBP_-0xc&&log_[8].fr[3]==G(0x41b440));
    CHECK(log_[9].f==G(0x434a24)&&log_[9].a[0]==T1&&log_[9].a[2]==0x00000102u&&log_[9].a[3]==0x0000cdcdu);
    CHECK(r32(0,EBP_-0x40)==0x11223344u&&log_[9].fr[3]==G(0x41b45d)&&log_[9].fr[0]==P);
    /* 3. Target change T1 -> T2 with the player's weapon: counts move,
     *    P+9Bh <- 0, P+9Dh <- 1, 40e484(PW, T2) with ECX = the new target,
     *    ESI = T2 from here on. */
    build();w32(0,P+0x1a4,T1);w32(0,H+0x210,T2);w32(0,P+0x30b,PW);w16(0,T1+0x1a2,5);w16(0,T2+0x1a2,0xffff);
    arena[P-OBJ+0x9b]=1;reset();
    v=es2_cockpit_sync_registers(&memory,&g,NULL,0x77,0x88,P,entry);
    CHECK(r32(0,P+0x1a4)==T2&&r16(0,T1+0x1a2)==4&&r16(0,T2+0x1a2)==0&&arena[P-OBJ+0x9b]==0&&arena[P-OBJ+0x9d]==1);
    CHECK(log_[2].f==G(0x49288c)&&log_[2].a[0]==P+0x26&&log_[2].a[1]==T1+0x26);
    CHECK(log_[7].f==G(0x40e484)&&log_[7].a[0]==PW&&log_[7].a[1]==T2&&log_[7].r[0]==PW&&log_[7].r[1]==P+0x92&&log_[7].r[2]==T2);
    CHECK(log_[8].f==G(0x43d9b4)&&rec32(log_+8,6)==T2&&log_[8].rec[0x25]==0);
    CHECK(log_[9].f==G(0x41b728)&&log_[10].f==G(0x434a24)&&log_[10].a[0]==T2&&calls==11);
    /* 4. Target lost with W+14h: the countdown word is armed (4500) and
     *    counted the same frame; ECX at the countdown is the old target. */
    build();w32(0,P+0x1a4,T1);w32(0,H+0x210,0);arena[W-OBJ+0x14]=1;w16(0,T1+0x1a2,1);timer_ax=3;reset();
    v=es2_cockpit_sync_registers(&memory,&g,NULL,0x77,0x88,P,entry);
    CHECK(r32(0,P+0x1a4)==0&&r16(0,T1+0x1a2)==0&&r16(0,P+0x31c)==4500&&arena[P-OBJ+0x9d]==1);
    CHECK(log_[7].f==G(0x467a48)&&log_[7].r[2]==T1&&log_[8].f==G(0x43d9b4)&&log_[9].f==G(0x434a24)&&calls==10);
    /* The getters' returned EAX is the record source, not G+0B1h. */
    build();record_at=0x20;reset();
    v=es2_cockpit_sync_registers(&memory,&g,NULL,0x77,0x88,P,entry);
    CHECK(rec32(log_+4,0)==(uint32_t)(200*1024/800)&&rec32(log_+4,4)==0x2a5&&r16(0,P+0x290)==0x2a5);
    CHECK(log_[6].f==G(0x43d9b4)&&rec32(log_+6,0xa)==r32(0,HUD+0xb1+0x20+0xa)&&rec32(log_+6,0x1c)==r32(0,HUD+0xb1+0x20+0x1c));
    record_at=0;
    /* throttle_gauge: a new set point calls the slider's slot +14h with
     * (S, DX = the set point), a new ratio marks the redraw byte; the
     * record is stored; EDX:EAX = [L]:[L+4]. */
    build();reset();w32(0,STACK,0x99);w32(0,STACK+4,0xfffe1234u);
    v=es2_throttle_gauge_registers(&memory,&g,TG,STACK,entry);
    CHECK(calls==1&&log_[0].f==0xc0000014u&&log_[0].slot==0x14&&log_[0].a[0]==S&&log_[0].a[1]==((entry[1]&0xffff0000u)|0x1234u));
    CHECK(log_[0].r[0]==K&&log_[0].r[1]==log_[0].a[1]&&log_[0].r[2]==S&&arena[S-OBJ+0x1f]==2&&r32(0,TG+0xb1)==0x99&&r32(0,TG+0xb5)==0xfffe1234u);
    CHECK(v==((uint64_t)0x99<<32|0xfffe1234u));
    build();reset();w32(0,STACK,0x77);w32(0,STACK+4,0x300);
    v=es2_throttle_gauge_registers(&memory,&g,TG,STACK,entry);
    CHECK(calls==0&&arena[S-OBJ+0x1f]==0&&v==((uint64_t)0x77<<32|0x300));
    /* The record getters. */
    CHECK(es2_throttle_record_registers(0x1000u,entry)==((uint64_t)entry[1]<<32|0x10b1u));
    CHECK(es2_hud_record_registers(0xffffff60u,entry)==((uint64_t)entry[1]<<32|0x11u));
    printf("PASS: cockpit_sync %u checks\n",checks);
    return 0;
}
