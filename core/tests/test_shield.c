#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "es2/core/shield.h"
/* Synthetic state for the core (shield_balance 413bc8). The image
 * base is placed so that the cockpit pointer 49b088 falls inside the
 * arena. The gauge setter 443880 is a small model that logs its address,
 * arguments, entry registers and the 15 record bytes it receives. All
 * values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x9b000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x1000u)
#define COCKPIT OBJECTS
#define GAUGE (OBJECTS+0x400u)
#define VEHICLE (OBJECTS+0x800u)
#define SCRATCH (OBJECTS+0xc00u)
static uint8_t arena[0x2000];static unsigned checks,ncalls,nsteps;
static uint32_t calls[4],args[4][2],regs[4][3];static uint8_t record[4][15];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"shield line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<4&&f==VA(0x443880)&&n==2);calls[ncalls]=f;
    args[ncalls][0]=a[0];args[ncalls][1]=a[1];
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;
    for(unsigned i=0;i<15;++i)record[ncalls][i]=r8(a[1]+i);
    ++ncalls;r->eax=a[0];r->edx=0x5eed0000u+ncalls;r->ecx=0xc0000000u;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(slot==0);if(f==VA(0x413af8))++nsteps;else CHECK(f==VA(0x443880));}
static void setup(int16_t front,int16_t rear,int16_t balance,int16_t maximum,uint8_t up,uint8_t down){
    w32(0,VA(0x49b088),COCKPIT);w32(0,COCKPIT+0x1e9,GAUGE);
    for(unsigned i=0;i<15;++i)w8(GAUGE+0xb5+i,(uint8_t)(0x40+i));
    w8(GAUGE+0xc2,up);w8(GAUGE+0xc3,down);
    w16(0,VEHICLE,(uint16_t)front);w16(0,VEHICLE+2,(uint16_t)rear);w16(0,VEHICLE+4,(uint16_t)balance);w16(0,VEHICLE+8,(uint16_t)maximum);
    w8(SCRATCH+15,0xee);ncalls=nsteps=0;
}
static uint64_t run(void){
    es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,0,observe,0};
    return es2_shield_balance_registers(&memory,&g,VEHICLE);
}
static int32_t rec32(unsigned c,unsigned o){return (int32_t)((uint32_t)record[c][o]|(uint32_t)record[c][o+1]<<8|(uint32_t)record[c][o+2]<<16|(uint32_t)record[c][o+3]<<24);}
int main(void){
    /* No request: front/rear in Q10 of the maximum, the balance passed. */
    setup(300,-150,0x200,600,0,0);
    uint64_t v=run();
    CHECK(ncalls==1&&nsteps==0&&(uint32_t)v==GAUGE&&(uint32_t)(v>>32)==0x5eed0001u);
    CHECK(args[0][0]==GAUGE&&args[0][1]==SCRATCH);
    CHECK(regs[0][0]==SCRATCH&&regs[0][1]==COCKPIT&&regs[0][2]==GAUGE);
    CHECK(rec32(0,0)==512&&rec32(0,4)==-256&&rec32(0,8)==0x200);
    CHECK(record[0][12]==0x4c&&record[0][13]==0&&record[0][14]==0&&r8(SCRATCH+15)==0xee);
    CHECK(r16(0,VEHICLE+4)==0x200&&r8(GAUGE+0xc2)==0&&r8(GAUGE+0xc3)==0);
    /* Truncation toward zero for a negative quotient and divisor. */
    setup(-7,7,0,-3,0,0);run();
    CHECK(rec32(0,0)==2389&&rec32(0,4)==-2389);
    /* Up request: +66h, clamped to 400h; the request byte is cleared in L
     * only (the gauge keeps its byte until 443880 copies L back). */
    setup(0,0,0x3c0,1,7,0);run();
    CHECK(nsteps==1&&r16(0,VEHICLE+4)==0x400&&rec32(0,8)==0x400&&record[0][13]==0&&r8(GAUGE+0xc2)==7);
    setup(0,0,0x100,1,1,0);run();
    CHECK(r16(0,VEHICLE+4)==0x166&&rec32(0,8)==0x166);
    /* Both requests: only up; down waits a frame. */
    setup(0,0,0x100,1,1,1);run();
    CHECK(nsteps==1&&r16(0,VEHICLE+4)==0x166&&record[0][13]==0&&record[0][14]==1);
    /* Down request: -66h, clamped to 0 at and below 0. */
    setup(0,0,0x66,1,0,2);run();
    CHECK(nsteps==1&&r16(0,VEHICLE+4)==0&&record[0][14]==0);
    setup(0,0,0x67,1,0,2);run();
    CHECK(r16(0,VEHICLE+4)==1);
    /* 16-bit wrap: 7FC0h + 66h is negative and clamps to 0; a negative
     * start stepping up stays at 0 unless the sum turns positive. */
    setup(0,0,0x7fc0,1,1,0);run();
    CHECK(r16(0,VEHICLE+4)==0&&rec32(0,8)==0);
    setup(0,0,-0x30,1,1,0);run();
    CHECK(r16(0,VEHICLE+4)==0x36);
    setup(0,0,-0x8000,1,0,1);run();
    CHECK(r16(0,VEHICLE+4)==0x400);
    /* The balance word sign-extends into L+8 without a request. */
    setup(0,0,-5,1,0,0);run();
    CHECK(rec32(0,8)==-5);
    printf("shield: %u checks\n",checks);
    return 0;
}
