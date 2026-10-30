#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/hdd_orders.h"
/* Synthetic state for the core (HDD map order controls). The image
 * base is placed so that the globals 49b088, 49d6b0 and the text table
 * 4d1638 fall inside the arena; the map M, its eight mode buttons, the map
 * gadget G with its holder, the graphics context and the scratch lie
 * above them. A logging call function stands in for every game call
 * (target, arguments, count, entry EAX/EDX/ECX; fixed result registers).
 * Expected values are worked out by hand from the contract in cockpit.md
 *. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x100u-0x9b088u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define MAP (BASE+0x10000u)
#define BUTTON(i) (BASE+0x11000u+0x40u*(i))
#define GADGET (BASE+0x12000u)
#define HOLDER (BASE+0x12400u)
#define CONTEXT (BASE+0x12500u)
#define GRAPHICS (BASE+0x12600u)
#define SCRATCH (BASE+0x13000u)
static uint8_t arena[0x40000],saved[0x40000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"hdd_orders line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint8_t b(uint32_t a){return arena[a-BASE];}
typedef struct call { uint32_t va,arg[3],count,eax,edx,ecx,local[2]; } call;
static call calls[8];static unsigned call_count,observed;
/* One callee side effect: after call number effect_call (1-based) the
 * dword at effect_address becomes effect_value. */
static unsigned effect_call;static uint32_t effect_address,effect_value;
/* Results: EAX = A0h+n, EDX = D0h+n, ECX = C0h+n for the n-th call. */
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<8);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f-IMAGE+0x400000u;for(unsigned i=0;i<n&&i<3;++i)k->arg[i]=a[i];k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(k->va==0x486e7cu){k->local[0]=r32(0,a[0]);k->local[1]=r32(0,a[0]+4);}
    r->eax=0xa0u+call_count;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;
    if(call_count==effect_call)w32(0,effect_address,effect_value);
}
static void fake_observe(void *c,uint32_t f,unsigned slot){
    (void)c;CHECK(slot==0||slot==0x10);CHECK(call_count<8);(void)f;++observed;
}
static const es2_weapon_graph graph={IMAGE,SCRATCH,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(void){
    memset(arena,0xcc,sizeof(arena));call_count=observed=effect_call=0;
    w32(0,VA(0x49d6b0),HOLDER);w32(0,HOLDER+8,GADGET);
    w32(0,VA(0x49b088),CONTEXT);w32(0,CONTEXT+4,GRAPHICS);
    for(unsigned k=0;k<4;++k)w32(0,VA(0x4d1638)+4*k,0x7000u+k);
    for(unsigned i=0;i<8;++i){w32(0,MAP+0x3d+4*i,BUTTON(i));
        /* Button i: x 10+20i..30+20i, y 100..120. */
        w32(0,BUTTON(i)+4,10+20*i);w32(0,BUTTON(i)+8,100);w32(0,BUTTON(i)+12,30+20*i);w32(0,BUTTON(i)+16,120);}
    w32(0,MAP+0x35,0x3500);w32(0,MAP+0x39,0x3900);w32(0,MAP+8,0x8888);
}
static void check_call(unsigned n,uint32_t va,unsigned count,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t eax,uint32_t edx,uint32_t ecx){
    CHECK(n<call_count);const call *k=calls+n;
    CHECK(k->va==va&&k->count==count&&k->arg[0]==a0);
    if(count>1)CHECK(k->arg[1]==a1);
    if(count>2)CHECK(k->arg[2]==a2);
    CHECK(k->eax==eax&&k->edx==edx&&k->ecx==ecx);
}
/* Only the listed byte ranges may change. */
static void unchanged_except(const uint32_t *ranges,unsigned n){
    for(uint32_t j=0;j<sizeof(arena);++j){
        int allowed=0;for(unsigned k=0;k<n;++k)if(BASE+j-ranges[2*k]<ranges[2*k+1])allowed=1;
        if(!allowed&&arena[j]!=saved[j]){fprintf(stderr,"byte %x changed\n",BASE+j);CHECK(0);}
    }
}
static void test_map_mode(void){
    /* Mode 0Bh over 0Dh: cursor 2, label text 3, pick flags, change. */
    setup();w32(0,MAP+0x5d,0xd);memcpy(saved,arena,sizeof(arena));
    uint64_t v=es2_map_mode_registers(&memory,&graph,MAP,0xb,entry);
    CHECK(call_count==2&&observed==2);
    check_call(0,0x433e3c,1,2,0,0,0xfffffffcu,0xe1,0xe2);
    check_call(1,0x44dc6c,3,MAP,0x7003,0,3,0x7003,2);
    CHECK(b(MAP+0x103)==1&&b(MAP+0x380)==1&&b(MAP+0x104)==0);
    CHECK(r32(0,MAP+0x81)==0xd&&r32(0,MAP+0x5d)==0xb&&b(GADGET+0x10f)==1&&b(GADGET+0xb0)==2);
    CHECK(v==((uint64_t)0xd2<<32|0xd));
    {const uint32_t ok[]={MAP+0x103,2,MAP+0x380,1,MAP+0x81,4,MAP+0x5d,4,GADGET+0x10f,1,GADGET+0xb0,1};unchanged_except(ok,6);}
    /* Mode 0Eh unchanged: text 2, no redraw. */
    setup();w32(0,MAP+0x5d,0xe);memcpy(saved,arena,sizeof(arena));
    v=es2_map_mode_registers(&memory,&graph,MAP,0xe,entry);
    check_call(0,0x433e3c,1,2,0,0,0xffffffffu,0xe1,0xe2);
    check_call(1,0x44dc6c,3,MAP,0x7002,0,2,0x7002,2);
    CHECK(v==((uint64_t)0xd2<<32|0xe)&&b(GADGET+0x10f)==0xcc&&r32(0,MAP+0x81)==0xccccccccu);
    /* Mode -1 over 0Ch: cursor 0, flags, 44dd14 with the cursor call's
     * EAX/EDX and ECX 0. */
    setup();w32(0,MAP+0x5d,0xc);memcpy(saved,arena,sizeof(arena));
    v=es2_map_mode_registers(&memory,&graph,MAP,0xffffffffu,entry);
    CHECK(call_count==2);
    check_call(0,0x433e3c,1,0,0,0,0xfffffff0u,0xe1,0xe2);
    check_call(1,0x44dd14,1,MAP,0,0,0xa1,0xd1,0);
    CHECK(b(MAP+0x103)==0&&b(MAP+0x104)==1&&b(MAP+0x380)==0&&r32(0,MAP+0x5d)==0xffffffffu&&r32(0,MAP+0x81)==0xc);
    CHECK(v==((uint64_t)0xd2<<32|0xc));
    /* Mode 0Fh (just above the pick range) and 0Ah (just below). */
    setup();es2_map_mode_registers(&memory,&graph,MAP,0xf,entry);CHECK(calls[1].va==0x44dd14);
    setup();es2_map_mode_registers(&memory,&graph,MAP,0xa,entry);CHECK(calls[1].va==0x44dd14&&calls[0].eax==0xfffffffbu);
    /* The gadget is read before the calls. */
    setup();w32(0,MAP+0x5d,1);w32(0,HOLDER+8,GADGET+0x100);
    es2_map_mode_registers(&memory,&graph,MAP,0xd,entry);CHECK(b(GADGET+0x20f)==1&&b(GADGET+0x1b0)==2);
}
static void test_wingman_select(void){
    /* Old 1 with mode 0Ch, new 2. */
    setup();w32(0,MAP+0x105,1);w32(0,MAP+0x5d,0xc);memcpy(saved,arena,sizeof(arena));
    uint64_t v=es2_wingman_select_registers(&memory,&graph,MAP,2,entry);
    CHECK(call_count==4&&observed==4);
    check_call(0,0x44ede0,2,MAP,1,0,1,0xe1,0xe2);
    check_call(1,0x44d9f4,2,MAP,0xffffffffu,0,0xa1,0xd1,0xc1);
    check_call(2,0x44ede0,2,MAP,2,0,2,21,1);
    check_call(3,0x44ee00,1,MAP,0,0,0xa3,0xd3,0xc3);
    CHECK(b(MAP+42+0xa6)==0&&b(MAP+0x104)==0&&b(MAP+0x144)==1&&r32(0,MAP+0x15c)==0&&b(MAP+0x103)==0);
    CHECK(b(MAP+0x381)==1&&r32(0,MAP+0x105)==2&&b(GADGET+0x10f)==1&&b(GADGET+0xb0)==2);
    CHECK(v==((uint64_t)HOLDER<<32|GADGET));
    {const uint32_t ok[]={MAP+42+0xa6,1,MAP+0x103,2,MAP+0x144,1,MAP+0x15c,4,MAP+0x381,1,MAP+0x105,4,GADGET+0x10f,1,GADGET+0xb0,1};unchanged_except(ok,8);}
    /* None before, none after: only the disable-all. */
    setup();w32(0,MAP+0x105,0xffffffffu);memcpy(saved,arena,sizeof(arena));
    v=es2_wingman_select_registers(&memory,&graph,MAP,0xffffffffu,entry);
    CHECK(call_count==1);check_call(0,0x44ee24,1,MAP,0,0,0xffffffffu,0xe1,0xe2);
    CHECK(b(MAP+0x381)==0xcc&&v==((uint64_t)HOLDER<<32|GADGET));
    /* Old 0 without a map mode, new 0: the second toggle gets the first
     * one's EDX/ECX; nothing cleared. */
    setup();w32(0,MAP+0x105,0);w32(0,MAP+0x5d,0xffffffffu);
    es2_wingman_select_registers(&memory,&graph,MAP,0,entry);
    CHECK(call_count==3);check_call(1,0x44ede0,2,MAP,0,0,0,0xd1,0xc1);CHECK(b(MAP+0x144)==0xcc&&b(MAP+0xa6)==0xcc);
    /* Old -1, new 1 with the gadget moved by a callee: read at the end. */
    setup();w32(0,MAP+0x105,0xffffffffu);w32(0,HOLDER+8,GADGET+0x100);
    v=es2_wingman_select_registers(&memory,&graph,MAP,1,entry);
    check_call(0,0x44ede0,2,MAP,1,0,1,0xe1,0xe2);CHECK(v==((uint64_t)HOLDER<<32|(GADGET+0x100)));
    /* The index is reread after the mode call (which sets it to 3 here). */
    setup();w32(0,MAP+0x105,2);w32(0,MAP+0x5d,0xb);effect_call=2;effect_address=MAP+0x105;effect_value=3;
    es2_wingman_select_registers(&memory,&graph,MAP,0xffffffffu,entry);
    CHECK(b(MAP+126+0xa6)==0&&b(MAP+84+0xa6)==0xcc);check_call(2,0x44ee24,1,MAP,0,0,0xffffffffu,63,3);
}
static void test_order_cancel(void){
    setup();w32(0,MAP+0x105,2);memcpy(saved,arena,sizeof(arena));
    uint64_t v=es2_order_cancel_registers(&memory,&graph,MAP,entry);
    CHECK(call_count==2&&observed==2);
    check_call(0,0x44d9f4,2,MAP,0xffffffffu,0,0xe0,0xe1,0xe2);
    check_call(1,0x44da98,2,MAP,0xffffffffu,0,42,2,0xc1);
    CHECK(b(MAP+84+0xa6)==0&&b(MAP+0x104)==0&&b(MAP+0x381)==0&&b(MAP+0x144)==1&&r32(0,MAP+0x15c)==0&&b(MAP+0x103)==0);
    CHECK(v==((uint64_t)0xd2<<32|0xa2));
    {const uint32_t ok[]={MAP+84+0xa6,1,MAP+0x103,2,MAP+0x144,1,MAP+0x15c,4,MAP+0x381,1};unchanged_except(ok,5);}
}
static uint64_t areas(uint32_t code,uint32_t x,uint32_t y){
    w32(0,MAP+0x14e,code);w32(0,MAP+0x152,x);w32(0,MAP+0x156,y);arena[MAP+0x14d-BASE]=1;
    memcpy(saved,arena,sizeof(arena));
    uint64_t v=es2_map_areas_registers(&memory,&graph,MAP,entry);
    CHECK(observed==call_count&&b(MAP+0x14d)==0);
    return v;
}
static void test_map_areas(void){
    /* Neither gadget: EAX = the code, EDX = entry EDX. */
    setup();uint64_t v=areas(0x1234,15,110);
    CHECK(call_count==0&&v==((uint64_t)0xe1<<32|0x1234));
    {const uint32_t ok[]={MAP+0x14d,1};unchanged_except(ok,1);}
    /* The map gadget: the point becomes a pending click. */
    setup();v=areas(0x3900,0x11111111u,0x22222222u);
    CHECK(call_count==0&&r32(0,MAP+0x11d)==0x11111111u&&r32(0,MAP+0x121)==0x22222222u&&b(MAP+0x11c)==1);
    CHECK(b(GADGET+0x10f)==1&&b(GADGET+0xb0)==2&&v==((uint64_t)0x22222222u<<32|GADGET));
    /* Panel, miss on every button (y on the top edge): EDX = 8. */
    setup();w32(0,MAP+0x105,0);v=areas(0x3500,15,100);
    CHECK(call_count==0&&v==((uint64_t)8<<32|0x3500));
    setup();w32(0,MAP+0x105,0);v=areas(0x3500,15,120);CHECK(call_count==0);
    setup();w32(0,MAP+0x105,0);v=areas(0x3500,30,110);CHECK(call_count==0);   /* right edge of 0, left edge of 1 */
    setup();w32(0,MAP+0x105,0);v=areas(0x3500,170,110);CHECK(call_count==0);  /* right edge of 7 */
    /* Hit on button 3 (x 70..90) with mode 0Ah: map_mode(M, 0Dh). */
    setup();w32(0,MAP+0x105,1);w32(0,MAP+0x5d,0xa);arena[MAP+0x15a-BASE]=0;v=areas(0x3500,75,101);
    CHECK(call_count==1);check_call(0,0x44d9f4,2,MAP,0xd,0,0xd,0xd,MAP+0x3d+12);
    CHECK(v==((uint64_t)0xd1<<32|0x3500));
    /* No wingman: the label with text 0 and 1. */
    setup();w32(0,MAP+0x105,0xffffffffu);arena[MAP+0x15a-BASE]=1;v=areas(0x3500,169,119);
    CHECK(call_count==1);check_call(0,0x44dc6c,3,MAP,0x7000,1,BUTTON(7)+4,7,0x7000);
    CHECK(b(MAP+0x15a)==1&&v==((uint64_t)0xd1<<32|0x3500));
    /* Same mode with both send flags: the HDD button 0Dh; then the warp
     * with the map rectangle 100..301 x 50..250 and the offsets. */
    setup();w32(0,MAP+0x105,0);w32(0,MAP+0x5d,0xb);arena[MAP+0x15b-BASE]=1;arena[MAP+0x104-BASE]=2;arena[MAP+0x15a-BASE]=1;
    w32(0,MAP+0x71,100);w32(0,MAP+0x79,301);w32(0,MAP+0x75,50);w32(0,MAP+0x7d,250);
    w32(0,GRAPHICS+0x210,7);w32(0,GRAPHICS+0x214,9);w32(0,GRAPHICS+0x220,1000);w32(0,GRAPHICS+0x224,2000);
    v=areas(0x3500,31,119);
    CHECK(call_count==2);check_call(0,0x438d9c,3,0x8888,0xd,1,0xb,0x8888,MAP+0x41);
    check_call(1,0x486e7c,1,SCRATCH,0,0,2000,SCRATCH,1000);
    CHECK(calls[1].local[0]==(uint32_t)(200+7-1000)&&calls[1].local[1]==(uint32_t)(150+9-2000));
    CHECK(b(MAP+0x15a)==0&&v==((uint64_t)0xd2<<32|0x3500));
    /* Same mode, one send flag clear: only the warp; a negative half
     * width (arithmetic shift): x 10..-11 gives the centre -1. */
    setup();w32(0,MAP+0x105,0);w32(0,MAP+0x5d,0xb);arena[MAP+0x15b-BASE]=1;arena[MAP+0x104-BASE]=0;arena[MAP+0x15a-BASE]=1;
    w32(0,MAP+0x71,10);w32(0,MAP+0x79,0xfffffff5u);w32(0,MAP+0x75,0);w32(0,MAP+0x7d,0);
    w32(0,GRAPHICS+0x210,0);w32(0,GRAPHICS+0x214,0);w32(0,GRAPHICS+0x220,0);w32(0,GRAPHICS+0x224,0);
    v=areas(0x3500,31,119);
    CHECK(call_count==1&&calls[0].va==0x486e7cu&&calls[0].local[0]==0xffffffffu&&calls[0].local[1]==0);
    CHECK(calls[0].eax==0&&calls[0].ecx==0);
    /* Both gadget codes equal: the button hit, then the pending click. */
    setup();w32(0,MAP+0x39,0x3500);w32(0,MAP+0x105,0);w32(0,MAP+0x5d,0xb);arena[MAP+0x15a-BASE]=0;
    v=areas(0x3500,11,101);
    CHECK(call_count==1&&calls[0].va==0x44d9f4u&&b(MAP+0x11c)==1&&v==((uint64_t)101<<32|GADGET));
    /* A callee that rewrites M+39h and the gadget: both are read after
     * the call. */
    setup();w32(0,MAP+0x105,0);w32(0,MAP+0x5d,0xb);arena[MAP+0x15a-BASE]=0;effect_call=1;effect_address=MAP+0x39;effect_value=0x3500;
    v=areas(0x3500,11,101);CHECK(b(MAP+0x11c)==1&&v==((uint64_t)101<<32|GADGET));
}
/* hdd_key 449ff4: the HDD gauge H at MAP (its page array at
 * H+101h, the active index H+109h), the page P at BUTTON(0) with its
 * vtable at BUTTON(1) (slot +10h = 44cc68). */
static uint64_t key(uint32_t code){memcpy(saved,arena,sizeof(arena));return es2_hdd_key_registers(&memory,&graph,MAP,code,entry);}
static void key_setup(void){
    setup();w32(0,MAP+0x109,0);w32(0,MAP+0x105,0x5105);w32(0,MAP+0x101,BUTTON(0));
    w32(0,BUTTON(0),BUTTON(1));w32(0,BUTTON(1)+0x10,VA(0x44cc68));
}
static void test_hdd_key(void){
    static const struct { uint32_t code,button,eax,edx; } presses[]={
        {0x48,2,0x48,0x48},{0x1004a,6,0x1004a,0},{0x4b,4,0x4b,1},{0x4d,5,0x4d,3},{0x4e,7,0x4e,4},{0x50,3,0x50,6},
        {0xffff0002u,0xa,0xa,0xfffffffdu},{0x3,0xb,0xb,0xfffffffeu},{0x4,0xc,0xc,0xffffffffu},{0x0c,6,0x0c,0},{0x2000d,7,0x2000d,0},
    };
    for(unsigned i=0;i<sizeof(presses)/sizeof(presses[0]);++i){
        key_setup();uint64_t v=key(presses[i].code);
        CHECK(call_count==1&&observed==1);
        check_call(0,0x438d9c,3,MAP,presses[i].button,1,presses[i].eax,presses[i].edx,0xe2);
        CHECK(v==((uint64_t)0xd1<<32|1u));   /* AL = 1 over EAX A1h */
        unchanged_except(NULL,0);
    }
    /* W/I/S: page 1, then the view 2/1/0 with [H+105h] in EDX/EAX/ECX. */
    static const struct { uint32_t code,view,edx; } views[]={{0x11,2,0x11},{0x17,1,0},{0x1f,0,0}};
    for(unsigned i=0;i<3;++i){
        key_setup();effect_call=1;effect_address=MAP+0x105;effect_value=0x6105;uint64_t v=key(views[i].code);
        CHECK(call_count==2&&observed==2);
        check_call(0,0x44a60c,2,MAP,1,0,views[i].code,views[i].edx,0xe2);
        check_call(1,0x450b88,2,0x6105,views[i].view,0,views[i].view==1?0x6105:0xa1,views[i].view==2?0x6105:0xd1,views[i].view==0?0x6105:0xc1);
        CHECK(v==((uint64_t)0xd2<<32|1u));
    }
    /* The rest: the page's slot +10h(page, full code), EDX:EAX passed on. */
    static const uint32_t rest[]={0x49,0x4c,0x4f,0x51,0x47,0x12,0x1e,0x20,0x10,0x0e,0x0b,0x01,0x00,0x218,0xffff0018u};
    for(unsigned i=0;i<sizeof(rest)/sizeof(rest[0]);++i){
        key_setup();uint64_t v=key(rest[i]);
        CHECK(call_count==1&&observed==1);
        check_call(0,0x44cc68,2,BUTTON(0),rest[i],0,BUTTON(0),BUTTON(1),0);
        CHECK(v==((uint64_t)0xd1<<32|0xa1u));
        unchanged_except(NULL,0);
    }
}
int main(void){
    test_map_mode();test_wingman_select();test_order_cancel();test_map_areas();test_hdd_key();
    printf("hdd_orders: %u checks passed\n",checks);
    return 0;
}
