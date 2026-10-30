#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/hud_keys.h"
/* Synthetic guest for the HUD key handler: a 64 KiB arena at OBJ holds the
 * cockpit H, the display object D, the MFD, the panel gadgets and the hud_key
 * scratch; the image globals (0x49b088 H pointer, 0x4cf99c D pointer, 0x6cbee4
 * counter, 0x4d2538) sit at B+off. Game callees are logged with their arguments
 * and entry registers and return planned registers; 0x49630c writes a known
 * FS:0 head into the SEH record at f-0x28; the display-object getter
 * 0x429820 returns [0x4cf99c] (a guest call). */
#define B UINT32_C(0x10000000)
#define OBJ UINT32_C(0x90000000)
#define ARENA 0x10000u
static uint8_t arena[ARENA];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"hud_keys line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;if(a-OBJ<ARENA)return arena+(a-OBJ);
    fprintf(stderr,"hud_keys: unmapped %08x\n",a);exit(1);}
/* The four image globals we need are redirected into the arena. */
#define G_H    (OBJ+0xf000u)   /* [0x49b088] -> H */
#define G_D    (OBJ+0xf010u)   /* [0x4cf99c] -> D */
#define G_CNT  (OBJ+0xf020u)   /* [0x6cbee4] -> counter */
#define G_2538 (OBJ+0xf030u)   /* [0x4d2538] byte */
static uint32_t remap(uint32_t a){
    if(a==B+0x9b088u)return G_H;
    if(a==B+0xcf99cu)return G_D;
    if(a==B+0x2cbee4u)return G_CNT;
    if(a==B+0xd2538u)return G_2538;
    return a;
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,remap(a));return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,remap(a));return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,remap(a));p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,remap(a));for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void *host_r(void *c,uint32_t a){return host(c,remap(a));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host_r};
typedef struct logged { uint32_t f,a[3],r[3]; unsigned n,slot; } logged;
static logged log_[32];static unsigned calls,observed,last_slot;
static uint32_t ret[32][3];static uint32_t head_value=0xdeadbeefu;
/* The frame the core names at each call (stack-bridge path). */
static const es2_hud_key_frame *frame_seen;static es2_hud_key_frame frames[32];
#define SCRATCH (OBJ+0xe000u)
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls;l->f=f;l->n=n;l->slot=last_slot;
    memset(l->a,0,sizeof(l->a));for(unsigned i=0;i<n&&i<3;++i)l->a[i]=a[i];
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==B+0x9630cu){w32(0,SCRATCH+ES2_HUD_KEYS_FRAME-0x28u,head_value);}
    if(frame_seen)frames[calls]=*frame_seen;
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];
    if(f==B+0x29820u)r->eax=r32(0,B+0xcf99cu);           /* getter: EDX/ECX unchanged */
    ++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;CHECK(observed==calls);last_slot=slot;++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,SCRATCH,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x1111aaaau,0x2222bbbbu,0x3333ccccu};
static void reset(void){calls=observed=last_slot=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));memset(arena,0,sizeof(arena));}

#define H (OBJ+0x100u)      /* cockpit */
#define D (OBJ+0x1000u)     /* display object */
#define MFD (OBJ+0x1100u)   /* [H+0x1ed] */
#define PANEL (OBJ+0x1200u) /* [H+0x1f9] */
#define GAD (OBJ+0x1300u)   /* a generic gadget with a vtable */
#define VT  (OBJ+0x1400u)
#define WS  (OBJ+0x1500u)   /* weapon system */

static void setup(uint32_t mode){
    w32(0,G_H,H);w32(0,G_D,D);w32(0,G_CNT,OBJ+0xf100u);
    w32(0,D+0x14,mode);
    w32(0,H+0x1ed,MFD);w32(0,H+0x1f9,PANEL);
}

static void test_button_release(void){
    reset();
    uint32_t P=OBJ+0x2000u;w16(0,P+0x396,3);         /* three already pending */
    ret[0][0]=0x1000u;ret[0][1]=0x7777u;              /* clock returns eax, edx */
    es2_weapon_graph g=graph();
    uint64_t v=es2_button_release_registers(&memory,&g,P,0xabcd,entry);
    CHECK(calls==1&&log_[0].f==B+0x67828u);           /* clock call */
    CHECK((uint32_t)v==0x100au&&(v>>32)==0x7777u);    /* eax=clock+0xa, edx=clock's */
    CHECK(r16(0,P+0x396)==4);                          /* count bumped */
    CHECK(r32(0,P+0x356+8*3)==0xabcd&&r32(0,P+0x356+8*3+4)==0x100au);
}

static void test_button_press(void){
    es2_weapon_graph g=graph();uint32_t G=OBJ+0x3000u,Bo=OBJ+0x3100u;
    reset();w32(0,G_H,H);w32(0,H,OBJ+0x3200u);         /* [H+0] panel */
    w32(0,G+0x18+4*2,Bo);w16(0,Bo+0x1b,2);             /* state 2 -> skip */
    uint64_t v=es2_button_press_registers(&memory,&g,G,2,1,entry);
    CHECK(calls==0&&(uint32_t)v==2&&(v>>32)==Bo);
    reset();w32(0,G_H,H);w32(0,H,OBJ+0x3200u);
    w32(0,G+0x18+4*2,Bo);w16(0,Bo+0x1b,0);w32(0,Bo+0x17,VT);w32(0,VT+8,0x12340008u);
    ret[1][0]=0x2000u;                                 /* 4530bc clock+... via its own call chain: here 4530bc is logged */
    v=es2_button_press_registers(&memory,&g,G,2,1,entry);
    CHECK(calls==2);
    CHECK(log_[0].slot==8&&log_[0].a[0]==Bo&&log_[0].a[1]==1);   /* virtual +8(B, flag) */
    CHECK(log_[1].f==B+0x530bcu&&log_[1].a[0]==OBJ+0x3200u&&log_[1].a[1]==Bo); /* 4530bc(panel, B) */
}

static void test_panel_off(void){
    es2_weapon_graph g=graph();reset();setup(0);
    *(uint8_t*)host(0,H+0x20f)=1;uint32_t hd;
    uint64_t v=es2_hud_key_registers(&memory,&g,H,5,entry,&hd);
    CHECK(calls==1&&log_[0].f==B+0x9630cu);            /* 49630c still runs */
    CHECK((uint32_t)v==0&&hd==head_value&&(v>>32)==head_value);
}

static void test_weapon_key(void){
    es2_weapon_graph g=graph();reset();setup(0);         /* M=0 */
    w32(0,H+0x70+4*3,OBJ+0x5000u);                        /* element for key '5' (code 5) */
    uint32_t hd;uint64_t v=es2_hud_key_registers(&memory,&g,H,5,entry,&hd);
    CHECK(calls==4&&log_[1].f==B+0x29820u);
    CHECK(log_[2].f==B+0x32a50u&&log_[2].a[0]==H&&log_[2].a[1]==OBJ+0x5000u); /* panel_weapon */
    CHECK(log_[3].f==B+0x38d9cu&&log_[3].a[0]==OBJ+0x5000u&&log_[3].a[1]==0&&log_[3].a[2]==1);
    CHECK((uint32_t)v==(ret[3][0]&0xffffff00u|1u)&&hd==head_value);
}

static void test_clear_target(void){
    es2_weapon_graph g=graph();reset();setup(0);
    uint32_t hd;uint64_t v=es2_hud_key_registers(&memory,&g,H,0x27,entry,&hd);
    CHECK(calls==3&&log_[2].f==B+0x332dcu&&log_[2].a[0]==H&&log_[2].a[1]==0);
    CHECK((uint32_t)v==(ret[2][0]&0xffffff00u|1u));
}

static void test_f9(void){
    es2_weapon_graph g=graph();reset();setup(0);         /* M=0 -> req 5 */
    uint32_t hd;uint64_t v=es2_hud_key_registers(&memory,&g,H,0x43,entry,&hd);
    CHECK(calls==4&&log_[2].f==B+0x29820u&&log_[3].f==B+0x2a3f4u&&log_[3].a[0]==D&&log_[3].a[1]==5);
    CHECK((uint32_t)v==(ret[3][0]&0xffffff00u|1u));
    reset();setup(1);                                     /* M=1 -> no request */
    v=es2_hud_key_registers(&memory,&g,H,0x43,entry,&hd);
    CHECK(calls==2&&(uint32_t)v==1);                      /* only 49630c and the getter */
}

static void test_default_offer(void){
    es2_weapon_graph g=graph();reset();setup(0);          /* M != 1 */
    /* call0=49630c, call1 the getter; 446c38 (call2) returns AL=0;
     * [H+0x1d8]=0 (skipped, no call); the [H+0x1e1] gadget (call3, virtual
     * +0xc) returns AL=1. */
    w32(0,H+0x1e1,GAD);w32(0,GAD,VT);w32(0,VT+0xc,0x55550003u);
    ret[2][0]=0x200u;ret[3][0]=0x301u;
    uint32_t hd;uint64_t v=es2_hud_key_registers(&memory,&g,H,0x100,entry,&hd);
    CHECK(calls==4&&log_[2].f==B+0x46c38u);
    CHECK(log_[3].slot==0xc&&log_[3].a[0]==GAD&&log_[3].a[1]==0x100);
    CHECK((uint32_t)v==((0x301u&0xffffff00u)|1u));
}

/* The frame the stack bridge loads. F1 in display mode 3: the MFD
 * button (ESI = M), then the request 6 with ESI stepped to M-4 = -1 and the
 * request pushed above the getter; the caller's ESI at 49630c and the first
 * getter; return addresses after each CALL. Then the default chain in mode 1
 * (ESI = M at 446c38, M-1 = 0 at the [H+1F9h] offer). */
static void test_frame(void){
    es2_weapon_graph g=graph();reset();setup(3);es2_hud_key_frame fr;memset(&fr,0,sizeof(fr));frame_seen=&fr;
    uint32_t hd;es2_hud_key_frame_registers(&memory,&g,&fr,0xe51e51u,H,0x3b,entry,&hd);
    CHECK(calls==5&&log_[2].f==B+0x38d9cu&&log_[3].f==B+0x29820u&&log_[4].f==B+0x2a3f4u&&log_[4].a[1]==6);
    for(unsigned k=0;k<5;++k)CHECK(frames[k].call.ebx==H&&frames[k].call.edi==0x3b);
    CHECK(frames[0].call.esi==0xe51e51u&&frames[0].call.ret==B+0x32be1u&&frames[0].pushed==0);
    CHECK(frames[1].call.esi==0xe51e51u&&frames[1].call.ret==B+0x32c00u);
    CHECK(frames[2].call.esi==3&&frames[2].call.ret==B+0x32ce8u);
    CHECK(frames[3].call.esi==0xffffffffu&&frames[3].call.ret==B+0x32d28u&&frames[3].pushed==1&&frames[3].temp==6);
    CHECK(frames[4].call.esi==0xffffffffu&&frames[4].call.ret==B+0x32d2eu&&frames[4].pushed==0);
    reset();setup(1);memset(&fr,0,sizeof(fr));
    w32(0,PANEL,VT);w32(0,VT+0xc,0x55550003u);ret[2][0]=0x200u;ret[3][0]=0x301u;
    es2_hud_key_frame_registers(&memory,&g,&fr,0,H,0x2d,entry,&hd);
    CHECK(calls==4&&log_[2].f==B+0x46c38u&&frames[2].call.esi==1&&frames[2].call.ret==B+0x32f9fu);
    CHECK(log_[3].slot==0xc&&frames[3].call.esi==0&&frames[3].call.ret==B+0x330fbu);
    frame_seen=NULL;
}

int main(void){
    test_button_release();
    test_button_press();
    test_panel_off();
    test_weapon_key();
    test_clear_target();
    test_f9();
    test_default_offer();
    test_frame();
    printf("hud_keys: %u checks passed\n",checks);
    return 0;
}
