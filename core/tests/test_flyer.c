#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/flyer.h"
/* Synthetic checks of the core on an own arena with scripted game
 * callees: the call order of a level flight, the throttle step and the
 * speed approach, the lever mode, the three sources of the pose byte 19
 * (stack residue of N, of D, or the old pose byte) and the rotation-only
 * compose cases. The in-vitro harness compares against the original; these
 * values are hand-derived. Image globals map into the arena by choosing
 * the image base BASE - 90000h. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x90000u)
#define G(va) (IMAGE+(va)-0x400000u)
#define OBJ (BASE+0x48000u)
static uint8_t arena[0x50000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"flyer line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define DEVICE (OBJ+0x900)
static unsigned calls;static uint32_t functions[128];
/* Scripted callees: movement_step returns its word, the products are the
 * real Q8/Q10, heading_cos is 4000h, Euler gives an identity (mode 0) for
 * zero angles and a mode-2 diagonal otherwise (never byte 19),
 * rotate_point copies, matrix_angles writes 0/0/7. */
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;(void)n;CHECK(calls<128);
    const uint32_t va=f-IMAGE+0x400000u;functions[calls++]=va;
    switch(va){
    case 0x477904u:r->eax=DEVICE;break;
    case 0x467924u:r->eax=(uint32_t)(int32_t)(int16_t)a[0];break;
    case 0x47e0a0u:r->eax=(uint32_t)(int32_t)((int64_t)(int32_t)a[0]*(int32_t)a[1]>>8);break;
    case 0x47e0b0u:r->eax=(uint32_t)(int32_t)((int64_t)(int32_t)a[0]*(int32_t)a[1]>>10);break;
    case 0x47dff0u:r->eax=0x4000;break;
    case 0x47ebb8u:{
        const int zero=!r16(0,a[0])&&!r16(0,a[0]+2)&&!r16(0,a[0]+4);
        for(unsigned i=0;i<18;i+=2)w16(0,a[1]+i,(i==0||i==6||i==16)?0x4000:0);
        ((uint8_t *)host(0,a[1]))[18]=zero?0:2;break;}
    case 0x480304u:{uint32_t v[3];for(unsigned i=0;i<3;++i)v[i]=r32(0,a[0]+4*i);for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,v[i]);break;}
    case 0x47df19u:break;
    case 0x47f9a0u:w16(0,a[1],0);w16(0,a[1]+2,0);w16(0,a[1]+4,7);break;
    default:CHECK(!"unexpected callee");
    }
}
static const uint32_t C=OBJ,S=OBJ+0x100,M=OBJ+0x200,F=OBJ+0x300,O=OBJ+0x40c,SCRATCH=OBJ+0x1000;
static void model(void){
    static const uint16_t words[]={400,1500,400,200,400,100,0};
    for(unsigned i=0;i<7;++i)w16(0,M+2*i,words[i]);
    static const uint32_t dwords[]={0x2b3333u,500,16,5,4,60000,6000,1500,250,300};
    for(unsigned i=0;i<10;++i)w32(0,M+0xeu+4*i,dwords[i]);
}
static void level(void){
    memset(arena,0,sizeof(arena));model();
    w32(0,S+4,1000);w16(0,S+0x1e,0x200);w32(0,S+0x20,O);
    w32(0,O+0x22,1000);w16(0,O+0x26,1);arena[O+6+19-BASE]=0x5a;
    calls=0;
}
static uint64_t run(uint8_t padding,uint8_t composed){
    es2_weapon_graph g={IMAGE,SCRATCH,0,padding,composed,call,NULL,NULL,NULL};
    const uint32_t entry[3]={0xaaaa0001u,0xbbbb0002u,0xcccc0003u};
    return es2_flight_model_registers(&memory,&g,C,S,M,F,entry);
}
int main(void){
    /* Level flight, throttle 200h: target 250 + q10(1250, 300h) = 1187,
     * speed 1000 steps by M+0Ah = 100 to 1100; no turn, EDX = s16(1100). */
    level();uint64_t p=run(0x11,0x22);
    CHECK(r32(0,S+4)==1100&&r32(0,S+0x10)==1100&&p>>32==1100);
    static const uint32_t order[]={0x477904,0x47e0a0,0x47e0b0,0x47e0b0,0x467924,0x480304,0x47e0b0,0x47e0b0,0x480304,
        0x467924,0x467924,0x467924,0x480304,0x47e0b0,0x47e0a0,0x47e0b0,0x47e0b0,0x47e0b0,
        0x467924,0x467924,0x467924,0x467924,0x467924,0x467924,0x47ebb8,0x47f9a0,0x467924,0x47ebb8,0x47df19,0x480304,0x480304};
    CHECK(calls==sizeof(order)/sizeof(order[0]));
    for(unsigned i=0;i<calls;++i)CHECK(functions[i]==order[i]);
    /* All increments zero: D is an identity, so 47f058 copies R and the
     * pose keeps its old byte 19; the angles come from matrix_angles. */
    CHECK(arena[O+6+19-BASE]==0x5a&&arena[S+0x24+19-BASE]==0x5a&&r16(0,O+4)==7&&r16(0,O+0x26)==1);
    CHECK(r32(0,S+0x44)==0&&arena[G(0x4a087c)-BASE]==1);
    /* A pitch rate: D and R both general, N is computed and its byte 19 is
     * the residue at EBP-18Dh (padding). */
    level();w16(0,S+0x18,64);arena[O+6+18-BASE]=2;w16(0,O+6,0x4000);w16(0,O+6+6,0x4000);w16(0,O+6+16,0x4000);
    run(0x11,0x22);CHECK(arena[O+6+19-BASE]==0x11&&arena[S+0x24+19-BASE]==0x11);
    /* R an identity (mode 0), D general: the copy of D brings the residue
     * at EBP-1A1h (composed padding). */
    level();w16(0,S+0x18,64);run(0x11,0x22);CHECK(arena[O+6+19-BASE]==0x22);
    /* Lever mode: device byte +4 set and controls byte 1Ah = 2; lever
     * C+0Ch = 90h gives 480h, clamped to 400h; no Q8 call. */
    level();arena[DEVICE+4-BASE]=1;arena[G(0x4d1f52)-BASE]=2;w32(0,C+0xc,0x90);run(0,0);
    CHECK(r16(0,S+0x1e)==0x400&&functions[1]==0x47e0b0);
    /* Throttle rate -3000h: q8(100, -3000h) = -12C0h, stepped, 200h - 12C0h
     * clamps to FC00h; the target is then 250, speed approaches by 100. */
    level();w32(0,C+0xc,0xffffd000u);run(0,0);CHECK(r16(0,S+0x1e)==0xfc00&&r32(0,S+4)==900);
    /* Rotation-only compose: planar x planar, general x planar, identity. */
    const uint32_t a=OBJ+0x2000,b=OBJ+0x2040,o=OBJ+0x2080;
    memset(arena+(a-BASE),0,0xc0);
    w16(0,a,0x4000);w16(0,a+6,0x4000);w16(0,a+16,0x4000);arena[a+18-BASE]=1;
    w16(0,b,0);w16(0,b+2,0x4000);w16(0,b+4,0xc000);w16(0,b+6,0);arena[b+18-BASE]=1;arena[o+19-BASE]=0x77;
    es2_compose_rotation(&memory,a,b,o);
    CHECK(r16(0,o)==0&&r16(0,o+2)==0x4000&&r16(0,o+4)==0xc000&&r16(0,o+16)==0x4000&&arena[o+18-BASE]==1&&arena[o+19-BASE]==0x77);
    arena[a+18-BASE]=2;w16(0,a+8,0x123);w16(0,a+12,0x4000);es2_compose_rotation(&memory,a,b,o);
    CHECK(r16(0,o+8)==0x123&&r16(0,o+12)==0&&r16(0,o+14)==0x4000&&arena[o+18-BASE]==2&&arena[o+19-BASE]==0x77);
    arena[a+18-BASE]=0;arena[b+19-BASE]=0x99;es2_compose_rotation(&memory,a,b,o);
    CHECK(!memcmp(arena+(o-BASE),arena+(b-BASE),20));
    printf("PASS: flyer core %u synthetic checks\n",checks);
    return 0;
}
