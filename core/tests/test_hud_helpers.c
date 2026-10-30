#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/hud_helpers.h"
/* Synthetic state for the core (panel_visible 431410). The image base
 * is placed so that the cockpit pointer 49b088 and the dwords 4d253e..
 * 4d255d fall inside the arena; the cockpit H, its canvas C and the
 * argument rect lie above them. Expected values are worked out by hand
 * from the contract in hud.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x9b000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define H (BASE+0x40000u)
#define C (BASE+0x40400u)
#define RECT (BASE+0x40800u)
static uint8_t arena[0x41000],saved[0x41000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"hud_helpers line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
/* View: clip origin (100, 50) plus canvas origin (20, 10) = (120, 60),
 * size 4d253e/4d2542 = (200, 150), so x 120..320 and y 60..210. Scroll
 * offset (1000, 2000). The clip rect's far corner is junk (unused). */
static void setup(void){
    memset(arena,0xcc,sizeof(arena));
    w32(0,VA(0x49b088),H);arena[H+0x20f-BASE]=0;w32(0,H+4,C);
    w32(0,C+0x210,100);w32(0,C+0x214,50);w32(0,C+0x218,0xdeadbeefu);w32(0,C+0x21c,0x12345678u);
    w32(0,C+0x220,20);w32(0,C+0x224,10);
    w32(0,VA(0x4d253e),200);w32(0,VA(0x4d2542),150);
    w32(0,VA(0x4d2556),1000);w32(0,VA(0x4d255a),2000);
}
static void rect(uint32_t x1,uint32_t y1,uint32_t x2,uint32_t y2){w32(0,RECT,x1);w32(0,RECT+4,y1);w32(0,RECT+8,x2);w32(0,RECT+12,y2);}
static uint64_t visible(uint32_t edx){
    memcpy(saved,arena,sizeof(arena));
    uint64_t v=es2_panel_visible_registers(&memory,IMAGE,RECT,edx);
    CHECK(!memcmp(saved,arena,sizeof(arena)));
    return v;
}
#define EAX(v) ((uint32_t)(v))
#define EDX(v) ((uint32_t)((v)>>32))
int main(void){
    /* Top-left corner inside (moved (130, 70)): 1, EDX = [4d2556]. */
    setup();rect(1130,2070,9999,9999);uint64_t v=visible(0x5555);
    CHECK(EAX(v)==1&&EDX(v)==1000);
    /* Edges are inclusive: (120, 60) and (320, 210). */
    rect(1120,2060,0,0);v=visible(0);CHECK(EAX(v)==1&&EDX(v)==1000);
    rect(1320,2210,0,0);v=visible(0);CHECK(EAX(v)==1&&EDX(v)==1000);
    /* Panel off: 0 with the entry EDX, whatever the rect. */
    arena[H+0x20f-BASE]=7;rect(1130,2070,1130,2070);v=visible(0xabcdef01u);
    CHECK(EAX(v)==0&&EDX(v)==0xabcdef01u);
    /* Top-left one below y1: the far corner (moved (200, 100)) is inside:
     * 1, EDX = moved x2 = 200. */
    setup();rect(1130,2059,1200,2100);v=visible(0);CHECK(EAX(v)==1&&EDX(v)==200);
    /* Far corner y below the view: 0, EDX = moved y2 = 59. */
    rect(1000,2000,1200,2059);v=visible(0);CHECK(EAX(v)==0&&EDX(v)==59);
    /* Far corner y above the view: 0, EDX = 211. */
    rect(1000,2000,1200,2211);v=visible(0);CHECK(EAX(v)==0&&EDX(v)==211);
    /* Far corner x left of the view: 0, EDX stays y2 = 100. */
    rect(1000,2000,1119,2100);v=visible(0);CHECK(EAX(v)==0&&EDX(v)==100);
    /* Far corner x right of the view: 0, EDX = x2 = 321. */
    rect(1000,2000,1321,2100);v=visible(0);CHECK(EAX(v)==0&&EDX(v)==321);
    /* Far corner on the edges: x2 = 120 and x2 = 320. */
    rect(321+1000,2000,1120,2210);v=visible(0);CHECK(EAX(v)==1&&EDX(v)==120);
    rect(321+1000,2000,1320,2060);v=visible(0);CHECK(EAX(v)==1&&EDX(v)==320);
    /* Signed tests: a view whose right edge wraps past 7FFFFFFFh is empty
     * for signed compares (x2 < x1); an unsigned test would accept 130. */
    setup();w32(0,VA(0x4d253e),0x7fffff90u);rect(1130,2070,1130,2070);v=visible(0);
    CHECK(EAX(v)==0&&EDX(v)==130);
    /* Negative coordinates: view x -80..120 with the clip origin at -100. */
    setup();w32(0,C+0x210,(uint32_t)-100);rect(1000-80,2070,0,0);v=visible(0);
    CHECK(EAX(v)==1&&EDX(v)==1000);
    /* Both corners left of the view: the x test fails after the y tests, EDX = y2. */
    rect(1000-81,2070,1000-81,2070);v=visible(0);CHECK(EAX(v)==0&&EDX(v)==70);
    /* The two scroll dwords are separate: a y offset of 0 moves only x. */
    setup();w32(0,VA(0x4d255a),0);rect(1130,70,0,0);v=visible(0);CHECK(EAX(v)==1&&EDX(v)==1000);
    printf("hud_helpers: %u checks passed\n",checks);
    return 0;
}
