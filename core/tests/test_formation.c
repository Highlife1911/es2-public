#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "es2/core/formation.h"
/* Synthetic state for the core (formation_base 405c04,
 * formation_mech 417898, formation_flyer 421e98). The image base is placed
 * so that the static buffers 4975d6/49a430 and the table pointers
 * 4a95ac/4a9d6c/4a9dfc fall inside the arena. euler_matrix 47ebb8 and
 * rotate_2d 47ffe8 are small models that log their address, arguments and
 * entry registers; the model rotation is x' = (x*m0 + y*m2 + 2000h) >> 14,
 * y' = (x*m1 + y*m3 + 2000h) >> 14 over the four matrix words. All values
 * are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x13000u)
#define ACTOR OBJECTS
#define GROUP (OBJECTS+0x100u)
#define LIST (OBJECTS+0x200u)
#define LEADER (OBJECTS+0x300u)
#define TABLE (OBJECTS+0x400u)
#define ROWS (OBJECTS+0x800u)
#define POINT (OBJECTS+0xc00u)
#define SCRATCH (OBJECTS+0xd00u)
static uint8_t arena[0x14000];static unsigned checks,ncalls,nnotes;
static uint32_t calls[4],args[4][3],regs[4][3],notes[8];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"formation line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static int32_t s16(uint16_t v){return v<0x8000u?(int32_t)v:(int32_t)v-0x10000;}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<4);calls[ncalls]=f;
    for(unsigned i=0;i<n;++i)args[ncalls][i]=a[i];
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;++ncalls;
    if(f==VA(0x47ebb8)){
        CHECK(n==2);for(unsigned i=0;i<4;++i)w16(0,a[1]+2*i,(uint16_t)(0x1000u*(i+1)));
        r->eax=0xe0;r->edx=0xe1;r->ecx=0xe2;
    } else {
        CHECK(f==VA(0x47ffe8)&&n==3);
        int64_t x=(int32_t)r32(0,a[0]),y=(int32_t)r32(0,a[0]+4);
        int64_t m0=s16(r16(0,a[1])),m1=s16(r16(0,a[1]+2)),m2=s16(r16(0,a[1]+4)),m3=s16(r16(0,a[1]+6));
        w32(0,a[2],(uint32_t)((x*m0+y*m2+0x2000)>>14));w32(0,a[2]+4,(uint32_t)((x*m1+y*m3+0x2000)>>14));
        r->eax=a[2];r->edx=0xf1;r->ecx=0xf2;
    }
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(slot==0&&nnotes<8);notes[nnotes++]=f;}
/* Actor with slot word, group of the given type, leader with cache flag;
 * the point P = (px, py, pz). */
static void setup(uint16_t slot,uint16_t type,uint16_t cached,uint32_t px,uint32_t py,uint32_t pz){
    for(unsigned i=0;i<0x1000;++i)arena[0x13000+i]=(uint8_t)(0x5a^i);
    w16(0,ACTOR+0x49,slot);w32(0,ACTOR+0x45,GROUP);w16(0,GROUP+2,type);w32(0,GROUP+0xc,LIST);w32(0,LIST,LEADER);
    w16(0,LEADER+0x32,cached);
    for(unsigned i=0;i<4;++i)w16(0,LEADER+0x12+2*i,0);
    w16(0,LEADER+0x12,0x4000);w16(0,LEADER+0x18,0x4000);
    w32(0,POINT,px);w32(0,POINT+4,py);w32(0,POINT+8,pz);
    ncalls=nnotes=0;
}
static uint64_t run(int which,uint32_t eax,uint32_t edx,uint32_t ecx){
    es2_weapon_graph g={IMAGE,SCRATCH,0,0,0,call,0,observe,0};const uint32_t e[3]={eax,edx,ecx};
    return which==0?es2_formation_base_registers(&memory,&g,ACTOR,POINT,e):
        which==1?es2_formation_mech_registers(&memory,&g,ACTOR,POINT,e):es2_formation_flyer_registers(&memory,&g,ACTOR,POINT,e);
}
int main(void){
    /* BASE: no slot returns the entry registers untouched. */
    setup(0,1,1,10,20,30);uint64_t v=run(0,0x11112222u,0x33334444u,0x55556666u);
    CHECK(v==((uint64_t)0x33334444u<<32|0x11112222u)&&ncalls==0&&nnotes==0&&r32(0,POINT)==10);
    /* MECH/FLYER: no slot clears AX only. */
    v=run(1,0x11112222u,0x33334444u,0x55556666u);CHECK(v==((uint64_t)0x33334444u<<32|0x11110000u)&&ncalls==0);
    v=run(2,0x1111ffffu,0x33334444u,0x55556666u);CHECK(v==((uint64_t)0x33334444u<<32|0x11110000u)&&ncalls==0);
    /* BASE: type 2, slot 3; row pointer at table + 28*2 + 4, entry row + 20. */
    setup(3,2,0,1000,2000,3000);
    w32(0,VA(0x4a95ac),TABLE);w32(0,TABLE+60,ROWS);w32(0,ROWS+20,500);w32(0,ROWS+24,(uint32_t)-700);
    w8(VA(0x4975e2),0);w32(0,VA(0x4975de),0xdeadbeefu);
    v=run(0,1,2,0xabcd0000u);
    /* First call: z zeroed, flag set; identity-like matrix 4000h on m0/m3
     * after the model euler_matrix writes 1000h..4000h. */
    CHECK(r8(VA(0x4975e2))==1&&r32(0,VA(0x4975de))==0&&r32(0,VA(0x4975d6))==500&&r32(0,VA(0x4975da))==(uint32_t)-700);
    CHECK(ncalls==2&&calls[0]==VA(0x47ebb8)&&calls[1]==VA(0x47ffe8));
    CHECK(args[0][0]==LEADER+0xc&&args[0][1]==LEADER+0x12);
    CHECK(regs[0][0]==LEADER+0x12&&regs[0][1]==LEADER+0xc&&regs[0][2]==LIST);
    CHECK(args[1][0]==VA(0x4975d6)&&args[1][1]==LEADER+0x12&&args[1][2]==SCRATCH);
    CHECK(regs[1][0]==LEADER+0x12&&regs[1][1]==VA(0x4975d6)&&regs[1][2]==SCRATCH);
    CHECK(r16(0,LEADER+0x32)==1);
    /* m0=1000h, m1=2000h, m2=3000h, m3=4000h: x' = (500*4096 - 700*12288 + 8192) >> 14. */
    int32_t rx=(int32_t)((500LL*0x1000-700LL*0x3000+0x2000)>>14),ry=(int32_t)((500LL*0x2000-700LL*0x4000+0x2000)>>14);
    CHECK(r32(0,POINT)==(uint32_t)(1000+rx)&&r32(0,POINT+4)==(uint32_t)(2000+ry)&&r32(0,POINT+8)==3000);
    CHECK(v==((uint64_t)(uint32_t)rx<<32|0));
    CHECK(nnotes==4&&notes[0]==VA(0x405b9c)&&notes[1]==VA(0x411d64)&&notes[2]==VA(0x47ebb8)&&notes[3]==VA(0x47ffe8));
    /* Second call: flag set, z kept (planted), cache valid: no euler_matrix. */
    setup(1,0,1,0,0,5);w32(0,VA(0x4a95ac),TABLE);w32(0,TABLE+4,ROWS);w32(0,ROWS,0x4000);w32(0,ROWS+4,0);
    w16(0,LEADER+0x12,0x4000);w16(0,LEADER+0x14,0);w16(0,LEADER+0x16,0);w16(0,LEADER+0x18,0x4000);
    w32(0,VA(0x4975de),77);v=run(0,0,0,0);
    CHECK(ncalls==1&&calls[0]==VA(0x47ffe8)&&r32(0,POINT)==0x4000&&r32(0,POINT+4)==0&&r32(0,POINT+8)==82&&(uint32_t)v==77);
    /* Negative type and slot words wrap in 32 bits: type FFFFh, slot FFFFh. */
    setup(0xffff,0xffff,1,0,0,0);w32(0,VA(0x4a95ac),TABLE+28);w32(0,TABLE+4,ROWS+40);w32(0,ROWS+20,9);w32(0,ROWS+24,0);
    run(0,0,0,0);CHECK(r32(0,VA(0x4975d6))==9);
    /* MECH: entry table + 28*1 + 4*2 - 4, words x=-3, y=4; z from 49a434. */
    setup(2,1,1,100,200,300);w32(0,VA(0x4a9d6c),TABLE);w8(VA(0x49a436),0);w16(0,VA(0x49a434),0x7777);
    w16(0,TABLE+32,0xfffd);w16(0,TABLE+34,4);
    w16(0,LEADER+0x12,0x4000);w16(0,LEADER+0x14,0);w16(0,LEADER+0x16,0);w16(0,LEADER+0x18,0x4000);
    v=run(1,0,0,0);
    CHECK(r8(VA(0x49a436))==1&&r16(0,VA(0x49a430))==0xfffd&&r16(0,VA(0x49a432))==4&&r16(0,VA(0x49a434))==0);
    CHECK(ncalls==1&&args[0][0]==SCRATCH+0x20&&regs[0][1]==SCRATCH+0x20);
    CHECK(r32(0,SCRATCH+0x20)==(uint32_t)-3&&r32(0,SCRATCH+0x24)==4&&r32(0,SCRATCH+0x28)==0);
    CHECK(r32(0,POINT)==97&&r32(0,POINT+4)==204&&r32(0,POINT+8)==300&&v==((uint64_t)(uint32_t)-3<<32|0));
    CHECK(nnotes==3&&notes[0]==VA(0x4205cc)&&notes[1]==VA(0x411d64));
    /* Second MECH call keeps the planted z word (sign-extended). */
    setup(1,0,1,0,0,0);w32(0,VA(0x4a9d6c),TABLE);w16(0,VA(0x49a434),0xfff0);w16(0,TABLE,0);w16(0,TABLE+2,0);
    w16(0,LEADER+0x12,0x4000);w16(0,LEADER+0x18,0x4000);
    v=run(1,0,0,0);CHECK(r32(0,POINT+8)==(uint32_t)-16&&(uint32_t)v==(uint32_t)-16);
    /* FLYER: entry table + 18*2 + 6*3 - 6 (x, y, z words), no buffer. */
    setup(3,2,1,0,0,1);w32(0,VA(0x4a9dfc),TABLE);
    w16(0,TABLE+48,8);w16(0,TABLE+50,0xfff8);w16(0,TABLE+52,0x8000);
    w16(0,LEADER+0x12,0x4000);w16(0,LEADER+0x14,0);w16(0,LEADER+0x16,0);w16(0,LEADER+0x18,0x4000);
    v=run(2,0,0,0);
    CHECK(r32(0,POINT)==8&&r32(0,POINT+4)==(uint32_t)-8&&r32(0,POINT+8)==(uint32_t)(1-0x8000)&&(uint32_t)v==(uint32_t)-0x8000);
    CHECK(nnotes==3&&notes[0]==VA(0x423044)&&notes[1]==VA(0x411d64)&&notes[2]==VA(0x47ffe8));
    printf("formation: %u checks\n",checks);
    return 0;
}
