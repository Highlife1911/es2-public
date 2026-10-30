#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/map_pick.h"
/* Synthetic state for the core (map_pick 44d888). The image base is
 * placed so that the actor list 4a9af8, its count word 4a9afe and the
 * display shift words 4d253a/4d253c fall inside the arena; the map, its
 * view object, the list array and the actors lie above them. Expected
 * values are worked out by hand from the contract in cockpit.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0xa9000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define MAP (BASE+0x2a000u)
#define VIEW (BASE+0x2a800u)
#define LIST (BASE+0x2a900u)
#define ACTOR(i) (BASE+0x2b000u+0x100u*(i))
static uint8_t arena[0x2c000],saved[0x2c000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"map_pick line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
/* Rectangle 100..300 x 50..250 (centre 200,150), map centre (1000, 2000,
 * scale 400h), shift 4, half-size words 2 and 3, the cursor at map+11Dh. */
static void setup(uint32_t px,uint32_t py,uint16_t count){
    memset(arena,0xcc,sizeof(arena));
    w32(0,MAP+0x14,VIEW);w32(0,VIEW+0x1a,4);
    w32(0,MAP+0x18,1000);w32(0,MAP+0x1c,2000);w32(0,MAP+0x20,0x400);
    w32(0,MAP+0x71,100);w32(0,MAP+0x75,50);w32(0,MAP+0x79,300);w32(0,MAP+0x7d,250);
    w32(0,MAP+0x11d,px);w32(0,MAP+0x121,py);
    w16(0,VA(0x4d253a),2);w16(0,VA(0x4d253c),3);
    w32(0,VA(0x4a9af8),LIST);w16(0,VA(0x4a9afe),count);
}
static void actor(unsigned slot,unsigned i,uint32_t x,uint32_t y){w32(0,LIST+4*slot,ACTOR(i));w32(0,ACTOR(i)+0x26,x);w32(0,ACTOR(i)+0x2a,y);}
static uint64_t pick(void){
    memcpy(saved,arena,sizeof(arena));
    uint64_t v=es2_map_pick_registers(&memory,IMAGE,MAP,MAP+0x11d);
    /* Only the stored point map+374h..+37Fh changes. */
    for(uint32_t j=0;j<sizeof(arena);++j)if(j-(MAP+0x374u-BASE)>=12u)CHECK(arena[j]==saved[j]);
    return v;
}
/* Map click 44d6e0: a logging call function in place of the game
 * calls. 44d888 returns the planned actor (EDX 5Eh, ECX 6Eh), 462830 and
 * 44dd14 return fixed registers; every call records its target, first two
 * arguments, count and entry EAX/EDX/ECX. The redraw flag 49d6ae lies at
 * BASE+100h through IMAGE2. Expected values worked out by hand from the
 * contract in cockpit.md. */
#define IMAGE2 (BASE+0x100u-0x9d6aeu)
typedef struct click_call { uint32_t va,arg0,arg1,count,eax,edx,ecx; } click_call;
static click_call calls[4];static unsigned call_count,observed;static uint32_t planned;
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<4);click_call *k=calls+call_count++;
    k->va=f-IMAGE2+0x400000u;k->arg0=a[0];k->arg1=n>1?a[1]:0;k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(k->va==0x44d888u){r->eax=planned;r->edx=0x5e;r->ecx=0x6e;}
    else if(k->va==0x462830u){r->eax=0xa1;r->edx=0xd1;r->ecx=0xc1;}
    else {CHECK(k->va==0x44dd14u);r->eax=0xa2;r->edx=0xd2;r->ecx=0xc2;}
}
static void fake_observe(void *c,uint32_t f,unsigned slot){
    (void)c;CHECK(slot==0&&(f==0x44d888u||f==0x462830u||f==0x44dd14u));++observed;
}
static const es2_weapon_graph graph={IMAGE2,0,0,0,0,fake_call,0,fake_observe,0};
#define SIDES (BASE+0x2a700u)
/* Map mode, wingman index, the picked actor (0 or ACTOR(0) with side byte
 * `side`). Returns EDX:EAX. */
static uint64_t click(uint32_t mode,uint32_t index,int hit,uint8_t side){
    memset(arena,0xcc,sizeof(arena));call_count=observed=0;
    w32(0,MAP+0x5d,mode);w32(0,MAP+0x105,index);arena[MAP+0x11c-BASE]=1;arena[0x100]=0;
    w32(0,MAP+0x374,0x11111111u);w32(0,MAP+0x378,0x22222222u);w32(0,MAP+0x37c,0x33333333u);
    w32(0,ACTOR(0)+0x45,SIDES);arena[SIDES+0x12-BASE]=side;planned=hit?ACTOR(0):0;
    memcpy(saved,arena,sizeof(arena));
    const uint32_t entry[3]={0xe0,0xe1,0xe2};
    uint64_t v=es2_map_click_registers(&memory,&graph,MAP,entry);
    CHECK(observed==call_count);
    CHECK(calls[0].va==0x44d888u&&calls[0].arg0==MAP&&calls[0].arg1==MAP+0x11d&&calls[0].count==2);
    CHECK(calls[0].eax==MAP+0x11d&&calls[0].edx==0xe1&&calls[0].ecx==0xe2);
    CHECK(arena[MAP+0x11c-BASE]==0&&arena[0x100]==1);
    return v;
}
/* Bytes changed by the click besides M+11Ch and the flag. */
static unsigned changed(void){
    unsigned n=0;for(uint32_t j=0;j<sizeof(arena);++j)if(arena[j]!=saved[j]&&j!=MAP+0x11cu-BASE&&j!=0x100u)++n;return n;
}
/* The two game calls after a store, with the store's EAX/EDX/ECX. */
static void stored(uint32_t eax,uint32_t edx,uint32_t ecx){
    CHECK(call_count==3);
    CHECK(calls[1].va==0x462830u&&calls[1].arg0==0x14&&calls[1].count==1&&calls[1].eax==eax&&calls[1].edx==edx&&calls[1].ecx==ecx);
    CHECK(calls[2].va==0x44dd14u&&calls[2].arg0==MAP&&calls[2].count==1&&calls[2].eax==0xa1&&calls[2].edx==0xd1&&calls[2].ecx==0x14);
}
static void map_click_tests(void){
    /* Mode 0Ch, side 0, wingman 1: the actor into record 1 (M+85h+2Ah):
     * +1Ch, kind +26h = 1, byte +21h = 1, M+15Ch, M+104h (14 bytes). */
    uint64_t v=click(0xc,1,1,0);
    stored(21,1,1);CHECK(v==((uint64_t)0xd2<<32|0xa2));
    CHECK(r32(0,MAP+0x85+0x2a+0x1c)==ACTOR(0)&&r32(0,MAP+0x85+0x2a+0x26)==1&&arena[MAP+0x85+0x2a+0x21-BASE]==1);
    CHECK(r32(0,MAP+0x15c)==ACTOR(0)&&arena[MAP+0x104-BASE]==1&&changed()==14);
    /* Mode 0Bh, side 1: the same store into record 0. */
    v=click(0xb,0,1,1);stored(0,0,0);CHECK(r32(0,MAP+0x85+0x1c)==ACTOR(0)&&r32(0,MAP+0x85+0x26)==1);
    /* Mode 0Bh, side 0 or FFh: nothing; EAX = T, EDX = the sign-extended
     * side byte. */
    v=click(0xb,0,1,0);CHECK(call_count==1&&changed()==0&&v==ACTOR(0));
    v=click(0xb,0,1,0xff);CHECK(call_count==1&&changed()==0&&v==((uint64_t)0xffffffffu<<32|ACTOR(0)));
    /* Mode 0Ch, side 0FFh (16-bit test FFFFh): the point. */
    v=click(0xc,0,1,0xff);stored(0,0,0);CHECK(r32(0,MAP+0x85+0xc)==0x11111111u&&r32(0,MAP+0x15c)==0xccccccccu);
    /* Mode 0Bh, no actor: nothing, EDX = mode-0Dh. */
    v=click(0xb,0,0,0);CHECK(call_count==1&&changed()==0&&v==(uint64_t)0xfffffffeu<<32);
    /* Mode 0Ch with side 1, or without an actor: the stored point into
     * record 2 (+0Ch..+17h), kind 0, byte +21h = 1. */
    v=click(0xc,2,1,1);stored(42,2,0);CHECK(v==((uint64_t)0xd2<<32|0xa2));
    CHECK(r32(0,MAP+0x85+0x54+0xc)==0x11111111u&&r32(0,MAP+0x85+0x54+0x10)==0x22222222u&&r32(0,MAP+0x85+0x54+0x14)==0x33333333u);
    CHECK(r32(0,MAP+0x85+0x54+0x26)==0&&arena[MAP+0x85+0x54+0x21-BASE]==1&&arena[MAP+0x104-BASE]==1&&changed()==12+4+1+1);
    v=click(0xc,0,0,0);stored(0,0,0);CHECK(r32(0,MAP+0x85+0xc)==0x11111111u);
    /* Modes 0Dh/0Eh: the point whatever was picked. */
    v=click(0xd,1,1,0);stored(21,1,0);CHECK(r32(0,MAP+0x85+0x2a+0x14)==0x33333333u&&r32(0,MAP+0x15c)==0xccccccccu);
    v=click(0xe,0,0,0);stored(0,0,0);
    /* Other modes: nothing; EDX = mode-0Fh, EAX = the pick. */
    v=click(0xa,0,1,0);CHECK(call_count==1&&changed()==0&&v==((uint64_t)0xfffffffbu<<32|ACTOR(0)));
    v=click(0xf,0,0,0);CHECK(call_count==1&&changed()==0&&v==0);
    /* Index -1 writes below the records: record -1 starts at M+5Bh, so
     * its point lands at M+67h, its kind dword at M+81h, its byte at
     * M+7Ch. EAX = 21*-1, EDX = -1. */
    v=click(0xd,0xffffffffu,0,0);stored(0xffffffebu,0xffffffffu,0);
    CHECK(r32(0,MAP+0x5b+0xc)==0x11111111u&&r32(0,MAP+0x5b+0x26)==0&&arena[MAP+0x5b+0x21-BASE]==1);
}
int main(void){
    /* Cursor (210, 140): offset (10, -10), y negated, x and y 10*400h>>4 =
     * 640, plus the centre: (1640, 2640, 400h). Half size 5<<2>>1 = 10, so
     * the left edge is (0*400h>>4)+1000 = 1000 and h = 640. Actor 0 lies
     * outside on x (EDX = its y+h after the failed first test), actor 1 at
     * (1700, 2700) is inside. */
    setup(210,140,3);actor(0,0,5000,0x12340000u);actor(1,1,1700,2700);actor(2,2,1640,2640);
    uint64_t v=pick();
    CHECK(r32(0,MAP+0x374)==1640&&r32(0,MAP+0x378)==2640&&r32(0,MAP+0x37c)==0x400);
    CHECK((uint32_t)v==ACTOR(1)&&(uint32_t)(v>>32)==2640);
    /* No hit, count 2 with actor 0 twice: EDX = high word of its y+h, low
     * word the counter of the last test (2). */
    setup(210,140,2);actor(0,0,5000,0x12340000u);actor(1,0,5000,0x12340000u);
    v=pick();
    CHECK((uint32_t)v==0&&(uint32_t)(v>>32)==0x12340002u);
    /* An empty list: EDX = high word of the list pointer, counter 0. */
    setup(210,140,0);v=pick();
    CHECK((uint32_t)v==0&&(uint32_t)(v>>32)==(LIST&0xffff0000u));
    /* A count word above the list: the null entry ends the walk; actor 0
     * passes the x test, fails the y test (EDX = map+378h = 2640, high
     * word 0), counter 1 at the null entry. */
    setup(210,140,5);actor(0,0,1700,9000);w32(0,LIST+4,0);
    v=pick();
    CHECK((uint32_t)v==0&&(uint32_t)(v>>32)==1);
    /* Edges are exclusive: an actor exactly at x-h (stored x = x-h). */
    setup(210,140,1);actor(0,0,1640+640,2640);v=pick();
    CHECK((uint32_t)v==0);
    setup(210,140,1);actor(0,0,1640+639,2640);v=pick();
    CHECK((uint32_t)v==ACTOR(0));
    /* Negative offsets shift arithmetically; the shift count is masked to
     * five bits (21h = 1), half-size counts 16 and above give 0, so h = 0
     * and nothing is hit. Cursor (190, 160): offset (-10, 10) -> y -10,
     * scaled -10*400h>>1 = -5120: stored (-4120, -3120). The first test
     * fails, EDX = y+h = -3120 keeps its high word FFFFh. */
    setup(190,160,1);w32(0,VIEW+0x1a,0x21);w16(0,VA(0x4d253a),16);actor(0,0,(uint32_t)-4120,(uint32_t)-3120);
    v=pick();
    CHECK(r32(0,MAP+0x374)==(uint32_t)-4120&&r32(0,MAP+0x378)==(uint32_t)-3120);
    CHECK((uint32_t)v==0&&(uint32_t)(v>>32)==0xffff0001u);
    /* Half-size word 15: 5<<15 keeps only bit 15 (8000h), sign-extended and
     * halved: -4000h, so the left edge lies right of the point (shift 0):
     * h = -4000h*400h = -1000000h < 0 and every test fails. */
    setup(200,150,1);w32(0,VIEW+0x1a,0);w16(0,VA(0x4d253a),15);actor(0,0,1000,2000);
    v=pick();
    CHECK(r32(0,MAP+0x374)==1000&&r32(0,MAP+0x378)==2000);
    CHECK((uint32_t)v==0);
    /* Same with word 14: 5<<14 = 14000h -> 4000h, halved 2000h; h =
     * 2000h*400h = 800000h, the actor at the centre is hit. */
    setup(200,150,1);w32(0,VIEW+0x1a,0);w16(0,VA(0x4d253a),14);actor(0,0,1000,2000);
    v=pick();
    CHECK((uint32_t)v==ACTOR(0)&&(uint32_t)(v>>32)==2000);
    /* An odd rectangle width halves arithmetically (width -3 -> -2). */
    setup(0,0,0);w32(0,MAP+0x71,10);w32(0,MAP+0x79,7);w32(0,MAP+0x75,0);w32(0,MAP+0x7d,0);w32(0,VIEW+0x1a,0);w32(0,MAP+0x20,1);
    w32(0,MAP+0x18,0);w32(0,MAP+0x1c,0);
    pick();
    CHECK(r32(0,MAP+0x374)==(uint32_t)-8&&r32(0,MAP+0x378)==0&&r32(0,MAP+0x37c)==1);
    map_click_tests();
    printf("PASS: map_pick and map_click %u checks\n",checks);
    return 0;
}
