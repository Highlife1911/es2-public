#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapons.h"
/* Synthetic state for the core beam_create 40b804. The image base is
 * placed so that the runtime counter pointer 6cbee4, the table pointer
 * 4a9804 and the RNG state 4d2599 fall inside the arena. The callees are
 * small models that log their entry registers; the RNG model counts up.
 * All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x2d0000u)
#define FRAME (BASE+0x2e0000u)
static uint8_t arena[0x2e1000];static unsigned checks,ncalls;
static uint32_t calls[64],regs[64][3],args[64][5],distance,rng_value;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"beam line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
#define ARRAY (OBJECTS+0x1000u)
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<64);calls[ncalls]=f;regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;
    memcpy(args[ncalls],a,4*n);++ncalls;
    uint32_t eax=0x1000u+ncalls,edx=0x2000u+ncalls,ecx=0x3000u+ncalls;
    if(f==VA(0x402188)){CHECK(n==2&&a[1]==0);}
    else if(f==VA(0x49288c)){CHECK(n==2);eax=distance;}
    else if(f==VA(0x49667c)){CHECK(n==5&&a[0]==0&&a[1]==12&&a[3]==1&&a[4]==VA(0x4953c4));eax=ARRAY;}
    else if(f==VA(0x4927f0)){CHECK(n==2&&a[1]==0x400);w32(0,a[0],0x400);w32(0,a[0]+4,0);w32(0,a[0]+8,0);}
    else if(f==VA(0x492ee0)){CHECK(n==1&&a[0]==VA(0x4d2599));eax=0xabcd0000u|(rng_value++);}
    else CHECK(0);
    r->eax=eax;r->edx=edx;r->ecx=ecx;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void setup(void){
    memset(arena,0,sizeof(arena));ncalls=0;rng_value=0xff80u;
    w32(0,VA(0x6cbee4),OBJECTS+0x800);w32(0,OBJECTS+0x800,100);
    w32(0,VA(0x4a9804),OBJECTS+0x900);
    w16(0,OBJECTS+0x900+6,0xfff0);w16(0,OBJECTS+0x900+42,0x0021);
}
int main(void){
    es2_weapon_graph g={IMAGE,FRAME,0,0,0,call,0,0,0};
    uint32_t o=OBJECTS,f=OBJECTS+0x600,t=OBJECTS+0x620;
    /* Two-point beam (type 3): F and T stored, counter +2. */
    setup();
    w32(0,f,10);w32(0,f+4,20);w32(0,f+8,30);w32(0,t,40);w32(0,t+4,50);w32(0,t+8,60);
    uint64_t v=es2_beam_create_registers(&memory,&g,o,0xbeef0003u,f,t,0x77778888u,entry);
    CHECK((uint32_t)v==o&&(uint32_t)(v>>32)==20);
    CHECK(ncalls==2&&calls[0]==VA(0x402188)&&calls[1]==VA(0x49667c));
    CHECK(regs[0][0]==entry[0]&&regs[0][1]==entry[1]&&regs[0][2]==entry[2]&&args[0][0]==o);
    CHECK(args[1][2]==2&&regs[1][0]==2&&regs[1][1]==0x77778888u&&regs[1][2]==(uint32_t)-4);
    CHECK(r32(0,o)==VA(0x4987c4)&&r16(0,o+4)==3&&arena[o+0x41-BASE]==3&&r32(0,o+0x4e)==0);
    CHECK(r16(0,o+0x52)==3&&r32(0,o+0x4a)==0x77778888u&&arena[o+0x54-BASE]==1&&r16(0,o+0x56)==2);
    CHECK(r32(0,o+0x58)==ARRAY&&arena[o+0x55-BASE]==0&&r16(0,o+0x5d)==0x38);
    CHECK(r32(0,ARRAY)==10&&r32(0,ARRAY+8)==30&&r32(0,ARRAY+12)==40&&r32(0,ARRAY+20)==60);
    CHECK(r32(0,o+0x26)==10&&r32(0,o+0x2a)==20&&r32(0,o+0x2e)==30&&r32(0,OBJECTS+0x800)==102);
    /* Stepped beam (type 1): d = 2047 -> e = 2, n = 2, six elements. */
    setup();distance=2047;
    w32(0,f,1000);w32(0,f+4,2000);w32(0,f+8,3000);w32(0,t,9000);w32(0,t+4,8000);w32(0,t+8,7000);
    v=es2_beam_create_registers(&memory,&g,o,0x00010001u,f,t,5,entry);
    CHECK((uint32_t)v==o&&(uint32_t)(v>>32)==2000);
    CHECK(ncalls==10&&calls[1]==VA(0x49288c)&&calls[2]==VA(0x49667c)&&calls[3]==VA(0x4927f0));
    CHECK(regs[1][0]==t&&regs[1][1]==f&&regs[1][2]==0&&args[1][0]==f&&args[1][1]==t);
    CHECK(args[2][2]==6&&regs[2][0]==6&&regs[2][1]==2&&regs[2][2]==0x3002u);
    CHECK(regs[3][0]==2000&&regs[3][1]==FRAME+0x0c&&regs[3][2]==1000&&args[3][0]==FRAME+0x0c);
    CHECK(arena[o+0x54-BASE]==2&&r16(0,o+0x56)==6&&arena[o+0x55-BASE]==1);
    /* i = 1 at cur = F + (400h, 0, 0); first RNG call's registers. */
    CHECK(regs[4][0]==3000&&regs[4][1]==2000&&regs[4][2]==2u*((FRAME+0x18)&0xffff0000u|1u));
    CHECK(regs[5][0]==(0xff80u&0x7f)&&regs[5][1]==0x2005u&&regs[5][2]==VA(0x4d2599));
    CHECK(regs[6][0]==(0xabcd0000u|(0xff81u&0x7f))&&regs[6][1]==(0xff81u&0x7f)&&regs[6][2]==VA(0x4d2599));
    /* i = 2 = n: P = T, ECX from T's high word. */
    CHECK(regs[7][2]==2u*((t&0xffff0000u)|2u));
    CHECK(r32(0,ARRAY)==1000&&r32(0,ARRAY+4)==2000&&r32(0,ARRAY+8)==3000-16);
    CHECK(r32(0,ARRAY+12)==1000&&r32(0,ARRAY+20)==3000);
    CHECK(r32(0,ARRAY+36)==1000+0x400+0&&r32(0,ARRAY+40)==2000+1&&r32(0,ARRAY+44)==3000+2);
    CHECK(r32(0,ARRAY+24)==r32(0,ARRAY+36)&&r32(0,ARRAY+32)==3000+2-16);
    CHECK(r32(0,ARRAY+60)==9000+3&&r32(0,ARRAY+64)==8000+4&&r32(0,ARRAY+68)==7000+5);
    CHECK(r32(0,ARRAY+56)==7000+5-16&&r32(0,OBJECTS+0x800)==102&&r16(0,o+0x5d)==0x38);
    /* Type 7: table word +42; wrapped n (d = 128 * 1024 - 1024 -> e = 128,
     * n = -128): count -254, no loop, no RNG. */
    setup();distance=127u*1024u;
    v=es2_beam_create_registers(&memory,&g,o,7,f,t,5,entry);
    CHECK(ncalls==4&&args[2][2]==(uint32_t)-254&&arena[o+0x54-BASE]==0x80&&r16(0,o+0x56)==(uint16_t)-254);
    /* d = 0: n = 1, the first element is F raised by +21h. */
    setup();distance=0;
    w32(0,f,1000);w32(0,f+4,2000);w32(0,f+8,3000);w32(0,t,9000);w32(0,t+4,8000);w32(0,t+8,7000);
    v=es2_beam_create_registers(&memory,&g,o,7,f,t,5,entry);
    CHECK(ncalls==7&&args[2][2]==4&&r32(0,ARRAY+8)==3000+0x21&&r32(0,ARRAY+20)==3000);
    printf("PASS: beam %u checks\n",checks);
    return 0;
}
