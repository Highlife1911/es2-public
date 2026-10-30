#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/hdd_map.h"
/* Synthetic guest: the map object and its records at 90000000h, a stack
 * page at A0000000h (the frame f and the argument slots), the image at B
 * (globals relocated). Calls are logged with their target, slot, arguments,
 * entry registers and the frame the core names; they return planned
 * registers. */
#define OBJ UINT32_C(0x90000000)
#define STACK UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define M (OBJ+0x100)
#define X (OBJ+0x1000)
#define F (STACK+0x80)
static uint8_t arena[8192],stack[256],globals[0x300000];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"hdd_map line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-STACK<sizeof(stack))return stack+(a-STACK);
    if(a-B>=0x99000u&&a-B<0x99000u+sizeof(globals))return globals+(a-B-0x99000u);
    fprintf(stderr,"hdd_map: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t *b8(uint32_t a){return host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[4],r[3],fr[4]; unsigned n,slot; } logged;
static logged log_[40];static unsigned calls,observed;
static es2_map_frame *active;
static uint32_t now,slot_after;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<40);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(active){l->fr[0]=active->call.ebx;l->fr[1]=active->call.esi;l->fr[2]=active->call.edi;l->fr[3]=active->call.ret;}
    if(active&&active->top)w32(0,active->top-4,slot_after+calls);
    r->eax=0x1000u+calls;r->edx=0x2000u+calls;r->ecx=0x3000u+calls;
    if(f==G(0x467828))r->eax=now;
    if(f==G(0x44d370))*b8(G(0x49d6ae))=1;   /* the tick requests the map redraw, as the original's */
    ++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,0,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(log_,0,sizeof(log_));}
#define BTN(i) (OBJ+0x800+0x40*(i))
#define ACT(i) (OBJ+0xc00+0x100*(i))
static void build(void){
    memset(arena,0,sizeof(arena));memset(stack,0,sizeof(stack));memset(globals,0,sizeof(globals));
    w32(0,G(0x6c5f70),X);
    for(unsigned i=0;i<8;++i){w32(0,M+0x3d+4*i,BTN(i));for(unsigned j=0;j<4;++j)w32(0,BTN(i)+4+4*j,0x100*i+j);}
    w32(0,M+0x31,OBJ+0x700);for(unsigned i=0;i<3;++i)w32(0,OBJ+0x700+4*i,BTN(10+i));
    for(unsigned k=0;k<18;++k){w32(0,G(0x4d12a8)+4*k,0x7700+k);w32(0,G(0x4d12f0)+4*k,OBJ+0x1800+k);arena[0x1800+k]=(uint8_t)(0x40+k);}
    w32(0,G(0x49b0ac),0xac);w32(0,G(0x49b0b0),0xb0);w32(0,G(0x49b0b4),0xb4);w32(0,G(0x49b0b8),0xb8);
    w32(0,M+0x105,0xffffffffu);w32(0,M+0x81,0xffffffffu);w32(0,M+0x5d,0xffffffffu);
}
int main(void){
    es2_weapon_graph g=graph();uint64_t p;
    /* Idle frame, state 0: the clock only (bit 4 clear: latch cleared); the
     * locals are written; EAX = state-3, EDX the clock's. */
    build();now=0x20;w32(0,M+0x149,0x20);*b8(G(0x49d7e6))=1;reset();
    p=es2_map_frame_registers(&memory,&g,NULL,F,0,0,M,entry);
    CHECK(calls==1&&log_[0].f==G(0x467828)&&log_[0].n==0&&log_[0].r[0]==entry[0]&&log_[0].r[2]==entry[2]);
    CHECK(*b8(G(0x49d7e6))==0&&r32(0,F-0xc)==0&&r32(0,F-4)==X);
    CHECK(p==((uint64_t)0x2000u<<32|0xfffffffdu));
    /* Event, rising edge of bit 4 with the counter at -5 (stored -1, EDX
     * 0FFFFFFFFh), timer due (tick with EAX = now+30), click with a
     * selected wingman and pick mode (map_click), dead target in the record
     * (cleared), state 2, blink, mode change 0Bh -> 0Ch, labels, map
     * redraw requested by the tick. */
    build();now=0x1234u|0x10u;w32(0,M+0x149,0x1000);w16(0,G(0x49d6b8),0xfffa);arena[M-OBJ+0x14d]=1;arena[M-OBJ+0x11c]=1;
    arena[M-OBJ+0x103]=1;w32(0,M+0x105,1);
    const uint32_t R=M+0x85+0x2a;w32(0,R+0x22,1);arena[R-OBJ+0x20]=1;arena[R-OBJ+0x21]=1;w32(0,R+0x18,ACT(0));w32(0,R+0x1c,ACT(1));
    arena[ACT(1)-OBJ+0x99]=1;w32(0,M+0xc,2);*b8(G(0x49d6ac))=1;w32(0,M+0x81,0xb);w32(0,M+0x5d,0xc);arena[M-OBJ+0x131+1]=1;
    arena[M-OBJ+0x144]=1;w16(0,G(0x4d3ba2),0x8001);
    es2_map_frame fr={{0,0,0,0},F-0x28};active=&fr;slot_after=0x5a000000u;reset();
    p=es2_map_frame_registers(&memory,&g,&fr,F,0x77,0x88,M,entry);active=NULL;
    CHECK(calls==12);
    CHECK(log_[0].f==G(0x44d450)&&log_[0].a[0]==M&&log_[0].r[0]==entry[0]&&log_[0].fr[0]==M&&log_[0].fr[1]==0x77&&log_[0].fr[2]==0x88&&log_[0].fr[3]==G(0x44c9a2));
    CHECK(log_[1].f==G(0x467828)&&log_[1].r[2]==slot_after+0&&log_[1].fr[3]==G(0x44c9a8));
    CHECK(r16(0,G(0x49d6b8))==0xffff&&*b8(G(0x49d7e6))==1);
    CHECK(log_[2].f==G(0x44d370)&&log_[2].r[0]==now+0x1e&&log_[2].r[1]==0xffffffffu&&r32(0,M+0x149)==now+0x1e);
    CHECK(log_[3].f==G(0x44d6e0)&&log_[3].r[0]==X&&log_[3].r[2]==0&&log_[3].fr[3]==G(0x44ca2d));
    CHECK(arena[R-OBJ+0x20]==0&&arena[R-OBJ+0x21]==0);
    CHECK(log_[4].f==G(0x44f644)&&log_[4].a[0]==BTN(11)&&log_[4].r[0]==1&&log_[4].r[1]==OBJ+0x700&&log_[4].r[2]==BTN(11));
    CHECK(arena[BTN(11)-OBJ+0x1f]==0&&*b8(G(0x49d6ac))==0);
    CHECK(r32(0,X+0x22c)==F-0xc);
    CHECK(r32(0,BTN(1))==0xb0&&r32(0,BTN(1)+0x21)==0xb4&&r32(0,BTN(2))==0xac&&r32(0,BTN(2)+0x21)==0xac);
    CHECK(log_[5].f==G(0x438aac)&&log_[5].a[0]==BTN(1)&&log_[5].a[1]==0x770b&&log_[5].a[2]==0x4b&&log_[5].r[0]==BTN(1)&&log_[5].r[2]==0x4b);
    CHECK(log_[6].f==G(0x44dd74)&&log_[6].n==4&&log_[6].a[1]==1&&log_[6].a[2]==0&&log_[6].a[3]==1&&log_[6].fr[1]==1&&log_[6].fr[3]==G(0x44cb9a));
    CHECK(log_[7].f==G(0x438aac)&&log_[7].a[0]==BTN(2)&&log_[7].a[2]==0x4c);
    CHECK(log_[8].f==G(0x44dd74)&&log_[8].a[1]==2&&log_[8].a[3]==(((0x1000u+7)&0xffffff00u)|1u)&&log_[8].r[0]==log_[8].a[3]);
    CHECK(r32(0,M+0x81)==0xc&&stack[F-STACK-0xd]==1);
    CHECK(log_[9].f==G(0x44dd14)&&log_[9].fr[1]==2);
    CHECK(log_[10].f==G(0x486704)&&log_[10].a[0]==M+0x71&&log_[10].r[1]==F-0xc&&log_[10].r[2]==X&&r32(0,F-8)==0xffff8001u);
    CHECK(log_[11].f==G(0x44e334)&&log_[11].r[2]==slot_after+10&&log_[11].fr[3]==G(0x44cc52));
    CHECK(r32(0,M+0xc)==0&&*b8(G(0x49d6ae))==0&&p==(0x1000u+11));
    /* Panel redraw (M+139h): 44de14 and the rectangle; a live target keeps
     * the record; no click when M+11Ch is clear; latch already set. */
    build();now=0x10;w32(0,M+0x149,0x10);*b8(G(0x49d7e6))=1;w16(0,G(0x49d6b8),3);w32(0,M+0x105,0);w32(0,M+0x85+0x22,1);
    arena[M-OBJ+0x85+0x20]=1;w32(0,M+0x85+0x18,ACT(0));w32(0,M+0xc,1);arena[M-OBJ+0x139]=1;reset();
    p=es2_map_frame_registers(&memory,&g,NULL,F,0,0,M,entry);
    CHECK(calls==3&&arena[M-OBJ+0x85+0x20]==1&&r16(0,G(0x49d6b8))==3);
    CHECK(log_[1].f==G(0x44de14)&&log_[2].f==G(0x43122c)&&log_[2].a[0]==1&&log_[2].a[1]==F-0x20&&log_[2].a[2]==0&&log_[2].r[0]==F-0x20);
    CHECK(r32(0,F-0x20)==0&&r32(0,F-0x1c)==1&&r32(0,F-0x18)==0x702&&r32(0,F-0x14)==0x703&&log_[2].r[2]==0x703&&log_[2].r[1]==0x702);
    CHECK(p==((uint64_t)(0x2000u+2)<<32|(0x1000u+2)));
    /* Click without pick mode: the wingman pick 44d82c; the counter wraps 3 -> 0. */
    build();now=0x10;w32(0,M+0x149,0x10);arena[M-OBJ+0x11c]=1;w16(0,G(0x49d6b8),3);reset();
    p=es2_map_frame_registers(&memory,&g,NULL,F,0,0,M,entry);
    CHECK(calls==2&&log_[1].f==G(0x44d82c)&&r16(0,G(0x49d6b8))==0&&log_[1].r[1]==0);
    printf("PASS: hdd_map %u checks\n",checks);
    return 0;
}
