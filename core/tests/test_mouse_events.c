#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mouse_events.h"
/* Synthetic guest for the core: objects at 90000000h, image base B
 * with windows for the globals 49db00h.. and 4d1d00h..4d2500h. Callees are
 * logged with target, slot, arguments and entry registers and return
 * planned registers. All values are synthetic. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[0x2000],win1[0x200],win2[0x900];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mouse_events line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x49db00)<sizeof(win1))return win1+(a-G(0x49db00));
    if(a-G(0x4d1d00)<sizeof(win2))return win2+(a-G(0x4d1d00));
    fprintf(stderr,"mouse_events: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,slot,a[2],r[3]; unsigned n; } logged;
static logged log_[16];static unsigned calls,observed;
static uint32_t ret[16][3],notes[16][2];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16&&n<=2);logged *l=log_+calls;l->f=f;l->n=n;memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    l->slot=notes[calls][1];r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(observed==calls&&observed<16);notes[observed][0]=f;notes[observed++][1]=slot;}
#define SCRATCH (OBJ+0x1f00)
static es2_weapon_graph graph(void){es2_weapon_graph g={B,SCRATCH,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
#define HA (OBJ+0x000)
#define HB (OBJ+0x010)
#define EA (OBJ+0x100)
#define EB (OBJ+0x200)
#define S (OBJ+0x400)
#define GAD (OBJ+0x800)
#define VT (OBJ+0x900)
static void reset(void){calls=observed=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));memset(notes,0,sizeof(notes));
    memset(arena,0,sizeof(arena));memset(win1,0,sizeof(win1));memset(win2,0,sizeof(win2));
    w32(0,G(0x4d1e04),HA);w32(0,G(0x4d1e08),HB);w32(0,HA+4,EA);w32(0,HB+4,EB);
    w32(0,G(0x49dbd8),S);w16(0,G(0x49dbdc),0xffff);w32(0,G(0x4d1dec),30);
    w32(0,S,5);w32(0,S+4,7);w32(0,S+0x22b,100);w32(0,S+0x22f,200);
    for(unsigned k=0;k<4;++k){w32(0,S+0x256+4*k,GAD+0x40*k);w32(0,GAD+0x40*k+0x17,VT);}
    for(unsigned k=0;k<8;++k)w32(0,VT+4*k,0x70000000u+k);}
static void event(uint32_t e,uint32_t x,uint32_t y,uint16_t b,uint32_t t){w32(0,e,x);w32(0,e+4,y);w16(0,e+8,b);w32(0,e+10,t);}
static int called(unsigned i,uint32_t f,unsigned slot,uint32_t a0,uint32_t a1,uint32_t eax,uint32_t edx,uint32_t ecx){
    const logged *l=log_+i;
    return i<calls&&l->f==f&&l->slot==slot&&l->a[0]==a0&&(l->n<2||l->a[1]==a1)&&l->r[0]==eax&&l->r[1]==edx&&l->r[2]==ecx&&
        notes[i][0]==(slot?f:f-B+0x400000u);
}
static uint64_t pr(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
static void dispatch(void){
    const es2_weapon_graph g=graph();uint32_t ecx;
    /* Empty write list: swap, the new write list emptied, EAX = EDX = new
     * read list, ECX = its count. */
    reset();w32(0,HA,0);w32(0,HB,5);
    uint64_t v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(v==pr(HA,HA)&&ecx==0&&calls==0&&r32(0,G(0x4d1e04))==HB&&r32(0,G(0x4d1e08))==HA&&r32(0,HB)==0);
    /* One left press at a new position: hit test (AL = 0), press. */
    reset();w32(0,HA,1);event(EA,130,250,1,1000);ret[0][0]=0xabcd0000u;ret[0][1]=0x1234u;
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==2&&called(0,G(0x4523b0),0,S,EA,S,S,100));
    CHECK(called(1,G(0x4527c8),0,S,EA,200,S,100));
    CHECK(r32(0,G(0x4d1de3))==30&&r32(0,G(0x4d1de7))==50&&r8(G(0x4d1dd4))==1&&r16(0,G(0x49db6c))==0&&r32(0,G(0x4d1dcc))==130);
    CHECK(v==pr(HA,HA)&&ecx==1);
    /* Hit test AL != 0: the move handler with 4523b0's EAX/EDX, ECX = S. */
    reset();w32(0,HA,1);event(EA,1,2,0,5);ret[0][0]=0x77777701u;ret[0][1]=0x2222u;
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==2&&called(1,G(0x45297c),0,S,EA,0x77777701u,0x2222u,S));
    /* Same position, left release within the double-click time: flag,
     * release handler 452898 (EAX = S, EDX/ECX = origin dwords). */
    reset();w32(0,HA,1);event(EA,9,9,0,40);w32(0,G(0x4d1dcc),9);w32(0,G(0x4d1dd0),9);w8(G(0x4d1dd4),1);w32(0,G(0x49dbe4),20);
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==1&&called(0,G(0x452898),0,S,EA,S,100,200)&&r8(G(0x49dbe0))==1&&r32(0,G(0x49dbe4))==40&&r8(G(0x4d1dd4))==0);
    /* Not within the time: no double click. State 2 is not "down". */
    reset();w32(0,HA,1);event(EA,9,9,0,60);w32(0,G(0x4d1dcc),9);w32(0,G(0x4d1dd0),9);w8(G(0x4d1dd4),1);w8(G(0x4d1dd5),2);w32(0,G(0x49dbe4),20);
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==1&&r8(G(0x49dbe0))==0&&r8(G(0x4d1dd5))==2);
    /* Drag mode, captured gadget 2: move -> drag virtual +18h with the
     * point in the scratch, redraw; release -> +1Bh cleared, redraw, +10h
     * and +8 with the sign-extended code; index -1, drag off. */
    reset();w32(0,HA,1);event(EA,50,60,0,1);w8(G(0x49dbde),1);w16(0,G(0x49dbdc),2);w8(G(0x4d1dd5),1);w16(0,GAD+0x80+0x1b,1);
    ret[3][0]=0x1234ff80u;
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==5&&called(0,0x70000006u,0x18,GAD+0x80,SCRATCH+8,GAD+0x80,2,VT)&&r32(0,SCRATCH+8)==45&&r32(0,SCRATCH+12)==53);
    CHECK(called(1,G(0x452ab8),0,S,GAD+0x80,S,S,GAD+0x80));
    CHECK(called(2,G(0x452ab8),0,S,GAD+0x80,S,S,GAD+0x80)&&r16(0,GAD+0x80+0x1b)==0);
    CHECK(called(3,0x70000004u,0x10,GAD+0x80,0,VT,2,S));
    CHECK(called(4,0x70000002u,0x8,GAD+0x80,0xffffff80u,0x1234ff80u,0xffffff80u,VT));
    CHECK(r16(0,G(0x49dbdc))==0xffff&&r8(G(0x49dbde))==0);
    /* Drag mode with no capture: nothing but the position. */
    reset();w32(0,HA,1);event(EA,50,60,0,1);w8(G(0x49dbde),1);
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==0&&r32(0,G(0x4d1de3))==0xffffffceu&&r32(0,G(0x4d1de7))==0xffffff74u);
    /* Playback with the follow flag: 486e7c(E) first. */
    reset();w32(0,HA,1);event(EA,3,4,0,1);w16(0,G(0x4d24d6),1);w8(G(0x49dbdf),1);
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==2&&called(0,G(0x486e7c),0,EA,0,EA,3,4)&&called(1,G(0x4523b0),0,S,EA,S,S,100));
    /* Two events: E advances by 14; ECX = the count. */
    reset();w32(0,HA,2);event(EA,1,1,0,1);event(EA+14,2,2,0,2);
    v=es2_mouse_dispatch_registers(&memory,&g,&ecx,entry);
    CHECK(calls==2&&called(1,G(0x4523b0),0,S,EA+14,S,S,100)&&ecx==2);
}
static void queue(void){
    uint32_t ecx;
    /* Full list: EAX 0, EDX/ECX unchanged. */
    reset();w32(0,HA,0x63);event(OBJ+0x1000,1,2,3,4);
    uint64_t v=es2_mouse_queue_registers(&memory,B,OBJ+0x1000,99,&ecx,entry);
    CHECK(v==pr(0,entry[1])&&ecx==entry[2]&&r32(0,HA)==0x63);
    /* Append at index 2. */
    reset();w32(0,HA,2);event(OBJ+0x1000,0x01020304u,0x05060708u,0x0a0b,0);
    v=es2_mouse_queue_registers(&memory,B,OBJ+0x1000,0xdeadbeefu,&ecx,entry);
    CHECK(r32(0,HA)==3&&r32(0,EA+28)==0x01020304u&&r32(0,EA+32)==0x05060708u&&r16(0,EA+36)==0x0a0b&&r32(0,EA+38)==0xdeadbeefu);
    CHECK(v==pr(((EA+28)&0xffffff00u)|1,0xdeadbeefu)&&ecx==0x01020304u);
    /* Over 99: still appended. */
    reset();w32(0,HA,0x64);event(OBJ+0x1000,1,2,3,4);
    v=es2_mouse_queue_registers(&memory,B,OBJ+0x1000,5,&ecx,entry);
    CHECK(r32(0,HA)==0x65&&r32(0,EA+14*0x64)==1);
    /* The event overlaps the slot: the word goes first, then the dwords
     * are read from the updated bytes. */
    reset();w32(0,HA,0);event(EA+4,0x11111111u,0x22222222u,0x3333,0);
    v=es2_mouse_queue_registers(&memory,B,EA+4,7,&ecx,entry);
    CHECK(r16(0,EA+8)==0x3333&&ecx==0x11111111u);
}
int main(void){
    dispatch();queue();
    printf("mouse_events: %u checks passed\n",checks);
    return 0;
}
