#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapons.h"
/* Synthetic state for the cores def_lookup 40ffc8, bullet_init 40af6c
 * and bullet_create 40b43c. The image base is placed so that the globals
 * 4a96c2, 4a96f8, 4a9700, 4a98fc, 4a9900, 4d2599 and 6cbee4 fall inside
 * the arena. The callees are small models that log their entry registers
 * and arguments. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x2d0000u)
#define FRAME (BASE+0x2e0000u)
static uint8_t arena[0x2e1000];static unsigned checks,ncalls;
static uint32_t calls[32],regs[32][3],args[32][3],rng_values[2],slot_value;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"bullet line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define O (OBJECTS+0x100u)
#define D (OBJECTS+0x300u)
#define REC (OBJECTS+0x400u)
#define OWNER (OBJECTS+0x500u)
#define VT (OBJECTS+0x600u)
#define POS (OBJECTS+0x700u)
#define ANG (OBJECTS+0x710u)
#define COUNTER (OBJECTS+0x800u)
#define SLOT38 (OBJECTS+0x900u)
static es2_weapon_frame frames[32],*live;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<32);calls[ncalls]=f;regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;
    if(n)memcpy(args[ncalls],a,4*n);if(live)frames[ncalls]=*live;++ncalls;
    uint32_t eax=0x1000u+ncalls,edx=0x2000u+ncalls,ecx=0x3000u+ncalls;
    if(f==VA(0x49630c)){CHECK(n==0);eax=0x12121212u;edx=r->edx;ecx=r->ecx;w32(0,FRAME+ES2_BULLET_CREATE_SCRATCH-0x24u,0xfeedf00du);}
    else if(f==VA(0x471b30)){CHECK(n==1&&a[0]==OBJECTS+0x40);eax=O;}
    else if(f==VA(0x40af6c)){CHECK(n==2&&a[0]==O);w32(0,O+0x4e,REC);}
    else if(f==VA(0x40adc0)){CHECK(n==1&&a[0]==O);eax=D;}
    else if(f==VA(0x492ee0)){CHECK(n==1&&a[0]==VA(0x4d2599));eax=0xabcd0000u|rng_values[(ncalls-1)%2];}
    else if(f==VA(0x49681a)){CHECK(n==3&&a[0]==O+0x42&&a[1]==0&&a[2]==8);memset(host(0,a[0]),0,8);eax=a[0];}
    else if(f==SLOT38){CHECK(n==1&&a[0]==OWNER);eax=slot_value;}
    else if(f==VA(0x4628e0)){CHECK(n==2&&a[1]==POS);ecx=0x5a5a0001u;}
    else if(f==VA(0x402188)){CHECK(n==2&&a[0]==O);}
    else if(f==VA(0x40ffc8)){CHECK(n==2&&a[0]==2);eax=REC;}
    else CHECK(0);
    r->eax=eax;r->edx=edx;r->ecx=ecx;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void setup(void){
    memset(arena,0,sizeof(arena));ncalls=0;live=NULL;
    w32(0,VA(0x6cbee4),COUNTER);w32(0,COUNTER,100);w32(0,VA(0x4a96c2),OBJECTS+0x40);
}
int main(void){
    es2_weapon_graph g={IMAGE,FRAME,0,0,0,call,0,0,0};
    /* def_lookup: three records, low words compared, count reread. */
    setup();w32(0,VA(0x4a98fc),REC);w16(0,VA(0x4a9900),3);
    w16(0,REC,2);w16(0,REC+2,5);w16(0,REC+0x24,4);w16(0,REC+0x26,5);w16(0,REC+0x48,4);w16(0,REC+0x4a,6);
    uint64_t v=es2_def_lookup_registers(&memory,IMAGE,0x77770004u,0x88880005u);
    CHECK((uint32_t)v==REC+0x24&&(uint32_t)(v>>32)==1);
    v=es2_def_lookup_registers(&memory,IMAGE,4,6);CHECK((uint32_t)v==REC+0x48&&(uint32_t)(v>>32)==2);
    v=es2_def_lookup_registers(&memory,IMAGE,4,7);CHECK(v==UINT64_C(0x300000000));
    w16(0,VA(0x4a9900),0xffff);v=es2_def_lookup_registers(&memory,IMAGE,2,5);CHECK(v==0);
    w16(0,VA(0x4a9900),0);v=es2_def_lookup_registers(&memory,IMAGE,2,5);CHECK(v==0);
    /* bullet_init: the shape of definition s16(type) = -1 (14 bytes below
     * the table), object_init, the vtables, def_lookup(2, type), the word
     * O+59h from bullet_def's record, the counter +2. */
    setup();w32(0,VA(0x4a96f8),D+0x40);w16(0,D+0x40-14,3);w32(0,VA(0x4a9700),OBJECTS+0xa00);w32(0,OBJECTS+0xa0c,0xdeadbeefu);
    w16(0,D+6,0x4321);
    v=es2_bullet_init_registers(&memory,&g,O,0x1234ffffu,entry);
    CHECK((uint32_t)v==O&&(uint32_t)(v>>32)==COUNTER&&r32(0,COUNTER)==102);
    CHECK(ncalls==3&&calls[0]==VA(0x402188)&&calls[1]==VA(0x40ffc8)&&calls[2]==VA(0x40adc0));
    CHECK(args[0][1]==0xdeadbeefu&&regs[0][0]==0xdeadbeefu&&regs[0][1]==OBJECTS+0xa00&&regs[0][2]==D+0x40);
    CHECK(args[1][1]==0x1234ffffu&&regs[1][0]==COUNTER&&regs[1][1]==0x1234ffffu&&regs[1][2]==0);
    CHECK(args[2][0]==O&&regs[2][0]==REC&&regs[2][1]==0x2002u&&regs[2][2]==0x3002u);
    CHECK(r32(0,O)==VA(0x498628)&&r16(0,O+4)==3&&arena[O+0x41-BASE]==0xff&&r32(0,O+0x4e)==REC&&r16(0,O+0x59)==0x4321);
    /* bullet_create: the full path with the division. */
    setup();live=&frames[31];
    w32(0,ANG,0x00200010u);w16(0,ANG+4,0x0030);w32(0,POS,11);w32(0,POS+4,22);w32(0,POS+8,33);
    w16(0,D+0xa,0x0081);w16(0,D+0xc,1);w16(0,D+2,0xfffd);w16(0,D+8,7);w16(0,REC+0xa,0x0050);
    w32(0,OWNER,VT);w32(0,VT+0x38,SLOT38);slot_value=0x7777000au;rng_values[0]=0x0155u;rng_values[1]=0x00ffu;
    memset(arena+(O+0x42-BASE),0xcc,8);
    uint32_t ecx=0;
    v=es2_bullet_create_registers(&memory,&g,live,0xb0b0b0b0u,0x5e5e5e5eu,0xd1d10000u,0xaaaa0003u,POS,ANG,OWNER,entry,&ecx);
    CHECK((uint32_t)v==O&&(uint32_t)(v>>32)==0xfeedf00du&&ecx==0x5a5a0001u);
    CHECK(ncalls==9&&calls[0]==VA(0x49630c)&&calls[1]==VA(0x471b30)&&calls[2]==VA(0x40af6c)&&calls[3]==VA(0x40adc0));
    CHECK(calls[4]==VA(0x492ee0)&&calls[5]==VA(0x492ee0)&&calls[6]==VA(0x49681a)&&calls[7]==SLOT38&&calls[8]==VA(0x4628e0));
    CHECK(regs[0][0]==VA(0x498549)&&regs[0][1]==entry[1]&&regs[0][2]==entry[2]);
    CHECK(frames[0].ebx==0xb0b0b0b0u&&frames[0].esi==0x5e5e5e5eu&&frames[0].edi==0xd1d10000u&&frames[0].ret==VA(0x40b44f));
    CHECK(regs[1][1]==OBJECTS+0x40&&regs[1][0]==0x12121212u&&frames[1].ret==VA(0x40b45b));
    CHECK(args[2][1]==((O&0xffff0000u)|3u)&&regs[2][0]==args[2][1]&&regs[2][1]==O&&regs[2][2]==OBJECTS+0x40);
    CHECK(frames[2].ebx==O&&frames[2].esi==0x5e5e5e5eu&&r32(0,COUNTER)==95);
    CHECK(regs[3][0]==O&&regs[3][1]==0x2003u&&regs[3][2]==COUNTER&&frames[3].ret==VA(0x40b49b));
    /* The angles copy and the first draw's registers. */
    CHECK(regs[4][0]==((ANG&0xffff0000u)|0x30)&&regs[4][1]==0x00000010u&&regs[4][2]==((O&0xffff0000u)|0x20));
    CHECK(frames[4].esi==D&&frames[4].edi==2u*0xd1d10081u);
    /* Draw 1: (0x0102 & 0x0155) - 0x81 = 0x7f -> 0x10 + 0x7f; draw 2:
     * (0x0102 & 0x00ff) - 0x81 = -0x7f -> 0x30 - 0x7f. */
    CHECK(r16(0,FRAME+ES2_BULLET_CREATE_SCRATCH-0x2c)==0x8f&&r16(0,FRAME+ES2_BULLET_CREATE_SCRATCH-0x2a)==0x20);
    CHECK(r16(0,FRAME+ES2_BULLET_CREATE_SCRATCH-0x28)==(uint16_t)(0x30-0x7f));
    CHECK(regs[5][0]==(0xabcd0000u|0x0155u)&&regs[5][2]==VA(0x4d2599)&&frames[5].edi==2u*((2u*0xd1d10081u&0xffff0000u)|0x81u));
    CHECK(r16(0,FRAME+ES2_BULLET_CREATE_SCRATCH-0x14)==8&&r32(0,FRAME+ES2_BULLET_CREATE_SCRATCH-4)==O);
    CHECK(r32(0,O+0x26)==11&&r32(0,O+0x2a)==22&&r32(0,O+0x2e)==33);
    CHECK(r16(0,O+0xc)==0x8f&&r16(0,O+0xe)==0x20&&r16(0,O+0x10)==(uint16_t)(0x30-0x7f)&&r16(0,O+0x32)==0);
    CHECK(r32(0,O+0x4a)==OWNER&&regs[6][0]==O+0x42&&regs[6][2]==11&&(uint16_t)regs[6][1]==(uint16_t)(0x30-0x7f));
    for(unsigned i=0;i<8;++i)CHECK(arena[O+0x42-BASE+i]==0);
    CHECK(regs[7][0]==OWNER&&regs[7][1]==0&&regs[7][2]==VT&&frames[7].ret==VA(0x40b545));
    CHECK(r16(0,O+0x52)==0x5a&&r16(0,O+0x54)==0&&r16(0,O+0x56)==0&&r32(0,O+0x5b)==0&&r16(0,O+0x5f)==0x400);
    CHECK(r16(0,O+0x61)==(uint16_t)(0x400/-3));
    CHECK(args[8][0]==0x11&&regs[8][0]==0x11&&regs[8][1]==7&&regs[8][2]==POS&&frames[8].ret==VA(0x40b588));
    /* Without the division word O+61h stays; a null frame record is fine. */
    setup();w32(0,ANG,0);w16(0,ANG+4,0);w16(0,D+0xa,0);w16(0,D+0xc,0);w16(0,D+8,0xfff6);
    w32(0,OWNER,VT);w32(0,VT+0x38,SLOT38);w16(0,O+0x61,0xabcd);
    v=es2_bullet_create_registers(&memory,&g,NULL,0,0,0,2,POS,ANG,OWNER,entry,&ecx);
    CHECK((uint32_t)v==O&&ncalls==9&&r16(0,O+0x61)==0xabcd&&args[8][0]==0&&regs[8][1]==(uint32_t)-10);
    CHECK(r16(0,FRAME+ES2_BULLET_CREATE_SCRATCH-0x2c)==0&&r16(0,FRAME+ES2_BULLET_CREATE_SCRATCH-0x28)==0);
    printf("PASS: bullet %u checks\n",checks);
    return 0;
}
