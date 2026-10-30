#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/panel_frame.h"
/* Synthetic guest: the cockpit and its objects at 90000000h, a stack slot
 * page at A0000000h, the image at B (globals relocated). Calls are logged
 * with their target, slot, arguments, entry registers and the frame the
 * core names; they return planned registers. */
#define OBJ UINT32_C(0x90000000)
#define STACK UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define H (OBJ+0x100)
#define VT (OBJ+0x1000)
static uint8_t arena[8192],stack[64],flag49b104;
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"panel_frame line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-STACK<sizeof(stack))return stack+(a-STACK);
    if(a==G(0x49b104))return &flag49b104;
    fprintf(stderr,"panel_frame: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[2],r[3],fr[4]; unsigned n,slot; } logged;
static logged log_[40];static unsigned calls,observed;
static es2_panel_frame *active;
static uint32_t valid_eax,slot_after;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<40);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(active){l->fr[0]=active->call.ebx;l->fr[1]=active->call.esi;l->fr[2]=active->call.edi;l->fr[3]=active->call.ret;}
    if(active&&active->top)w32(0,active->top-4,slot_after+calls);
    r->eax=0x1000u+calls;r->edx=0x2000u+calls;r->ecx=0x3000u+calls;
    if(f==G(0x433174))r->eax=valid_eax;
    if(f==G(0x433770))arena[H-OBJ+0x245]=1;  /* a callee effect seen by the next read */
    ++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,0,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=0;memset(log_,0,sizeof(log_));}
/* Object k at OBJ+0x400+0x40k with vtable VT+0x20k; slot +8 = 0xC0000000+k,
 * slot +14h = 0xD0000000+k. */
static uint32_t object(unsigned k){
    uint32_t o=OBJ+0x400+0x40*k,v=VT+0x20*k;w32(0,o,v);w32(0,v+8,0xc0000000u+k);w32(0,v+0x14,0xd0000000u+k);return o;
}
static void build(void){
    memset(arena,0,sizeof(arena));flag49b104=0;
    w32(0,H,object(20));
    for(unsigned i=0;i<10;++i)w32(0,H+0x70+4*i,i==3||i==7?0:object(i));
    static const uint32_t subs[6]={0x1e1,0x1e5,0x1e9,0x1f1,0x1ed,0x1f9};
    for(unsigned i=0;i<6;++i)w32(0,H+subs[i],object(10+i));
    w32(0,H+0x207,object(16));w32(0,H+0x210,OBJ+0x1800);
}
int main(void){
    es2_weapon_graph g=graph();uint64_t p;
    /* Byte 49B104h: nothing runs, the entry pair returns. */
    build();flag49b104=1;reset();
    p=es2_panel_frame_registers(&memory,&g,NULL,0,0,H,entry);
    CHECK(calls==0&&p==((uint64_t)entry[1]<<32|entry[0]));
    /* First frame, plain bridge: 433770 (sets +245h itself, so 432924 is
     * skipped), 43408c, 434194, port +14h, 4311e0, eight elements, six
     * sub-objects, 431210, 433c54, 433174 (AL 0: target cleared), 4530fc,
     * 4349ac. */
    build();reset();valid_eax=0xabcdef00u;
    p=es2_panel_frame_registers(&memory,&g,NULL,0x77,0x88,H,entry);
    CHECK(calls==24&&arena[H-OBJ+0x23e]==1&&r32(0,H+0x210)==0);
    CHECK(log_[0].f==G(0x433770)&&log_[0].a[0]==H&&log_[0].r[0]==entry[0]&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x43408c)&&log_[1].r[0]==0x1000&&log_[1].r[2]==H);
    CHECK(log_[2].f==G(0x434194));
    CHECK(log_[3].f==0xd0000010u&&log_[3].slot==0x14&&log_[3].a[0]==r32(0,H+0x207)&&log_[3].r[2]==r32(0,H+0x207)&&log_[3].r[0]==VT+0x20*16&&log_[3].r[1]==0x2002);
    CHECK(log_[4].f==G(0x4311e0)&&log_[4].n==0&&log_[4].r[2]==r32(0,H+0x207));
    unsigned j=5;
    for(unsigned i=0;i<10;++i){if(i==3||i==7)continue;
        CHECK(log_[j].f==0xc0000000u+i&&log_[j].slot==8&&log_[j].a[0]==r32(0,H+0x70+4*i)&&log_[j].r[0]==log_[j].a[0]&&log_[j].r[1]==VT+0x20*i);
        CHECK(log_[j].r[2]==log_[j-1].a[0]||j==5);++j;}
    CHECK(log_[5].r[2]==0x3004u);
    for(unsigned i=0;i<6;++i)CHECK(log_[j+i].f==0xc000000au+i&&log_[j+i].slot==8);
    CHECK(log_[j].r[2]==log_[j].a[0]&&log_[j].r[0]==VT+0x20*10);           /* ECX obj, EAX vtable */
    CHECK(log_[j+1].r[1]==log_[j+1].a[0]&&log_[j+1].r[2]==VT+0x20*11);     /* EDX obj, ECX vtable */
    CHECK(log_[j+2].r[0]==log_[j+2].a[0]&&log_[j+2].r[1]==VT+0x20*12);     /* EAX obj, EDX vtable */
    j+=6;
    CHECK(log_[j].f==G(0x431210)&&log_[j+1].f==G(0x433c54)&&log_[j+1].a[0]==H);
    CHECK(log_[j+2].f==G(0x433174)&&log_[j+2].n==2&&log_[j+2].a[0]==H&&log_[j+2].a[1]==OBJ+0x1800&&log_[j+2].r[0]==OBJ+0x1800&&log_[j+2].r[2]==H);
    CHECK(log_[j+3].f==G(0x4530fc)&&log_[j+3].a[0]==r32(0,H)&&log_[j+3].r[2]==r32(0,H)&&log_[j+3].r[1]==0);
    CHECK(log_[j+4].f==G(0x4349ac)&&log_[j+4].r[2]==r32(0,H));
    CHECK(p==((uint64_t)(0x2000u+23)<<32|(0x1000u+23)));
    /* Second frame: init done, startup pending, AL nonzero keeps the
     * target; the frame names EBX/ESI/EDI and return addresses, and ECX
     * after each one-slot call is the slot the callee left. */
    build();arena[H-OBJ+0x23e]=1;reset();valid_eax=0x100u|1u;
    es2_panel_frame fr={{0,0,0,0},STACK+32};active=&fr;slot_after=0x5a000000u;
    p=es2_panel_frame_registers(&memory,&g,&fr,0x77,0x88,H,entry);active=NULL;
    CHECK(calls==24&&log_[0].f==G(0x432924)&&r32(0,H+0x210)==OBJ+0x1800);
    CHECK(log_[0].fr[0]==H&&log_[0].fr[1]==0x77&&log_[0].fr[2]==0x88&&log_[0].fr[3]==G(0x4327e8));
    CHECK(log_[1].r[2]==slot_after+0&&log_[1].fr[3]==G(0x4327ef));
    CHECK(log_[4].fr[1]==0x77&&log_[4].fr[3]==G(0x432824));
    CHECK(log_[5].fr[1]==H+0x70&&log_[5].fr[2]==0&&log_[5].fr[3]==G(0x432835)&&log_[5].r[2]==0x3004u&&log_[6].r[2]==slot_after+5);
    CHECK(log_[13].fr[1]==H+0x98&&log_[13].fr[2]==10&&log_[13].fr[3]==G(0x43284b));
    CHECK(log_[21].fr[3]==G(0x4328aa)&&log_[22].r[1]==0x2000u+21&&log_[22].fr[3]==G(0x4328c1));
    CHECK(log_[23].r[2]==slot_after+22&&log_[23].fr[3]==G(0x4328c7));
    /* No target: 433174 is skipped. */
    build();arena[H-OBJ+0x23e]=1;arena[H-OBJ+0x245]=1;w32(0,H+0x210,0);reset();
    p=es2_panel_frame_registers(&memory,&g,NULL,0,0,H,entry);
    CHECK(calls==22&&log_[20].f==G(0x4530fc)&&log_[20].r[0]==0);
    /* Panel off: after the bump only [H+1F9h] +8, returning its pair. */
    build();arena[H-OBJ+0x23e]=1;arena[H-OBJ+0x245]=1;arena[H-OBJ+0x20f]=1;reset();
    p=es2_panel_frame_registers(&memory,&g,NULL,0,0,H,entry);
    CHECK(calls==3&&log_[2].f==0xc000000fu&&log_[2].r[0]==r32(0,H+0x1f9)&&log_[2].r[1]==VT+0x20*15&&log_[2].r[2]==H);
    CHECK(p==((uint64_t)0x2002u<<32|0x1002u));
    printf("PASS: panel_frame %u checks\n",checks);
    return 0;
}
