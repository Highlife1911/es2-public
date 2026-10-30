#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapons.h"
/* Synthetic state for the cores proj_record 40ffb0, damage_words
 * 40501c, pitch_point 49295c, bullet_charge 40b5a0 and rocket_init 40a948.
 * The image base is placed so that the globals 4a95f2, 4a96d0, 4a96d8,
 * 4a98fc and 6cbee4 fall inside the arena. The callees are small models
 * that log their entry registers and arguments. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x2d0000u)
static uint8_t arena[0x2e1000];static unsigned checks,ncalls;
static uint32_t calls[8],regs[8][3],args[8][4],counts[8],observed[8],nobserved;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"weapon_leaves line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define O (OBJECTS+0x100u)
#define P (OBJECTS+0x200u)
#define Q (OBJECTS+0x210u)
#define OWNER (OBJECTS+0x300u)
#define DEFS (OBJECTS+0x500u)
#define SHAPES (OBJECTS+0x600u)
#define COUNTER (OBJECTS+0x800u)
#define COUNTER2 (OBJECTS+0x804u)
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(slot==0&&nobserved<8);observed[nobserved++]=f;}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<8&&n<=4);calls[ncalls]=f;counts[ncalls]=n;regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;
    if(n)memcpy(args[ncalls],a,4*n);++ncalls;
    uint32_t eax=0x1000u+ncalls,edx=0x2000u+ncalls,ecx=0x3000u+ncalls;
    if(f==VA(0x492a9a))eax=0x00c0ffeeu;
    else if(f==VA(0x49290c)){eax=0x77771234u;edx=0x88885678u;ecx=0x99990001u;}
    else if(f==VA(0x40b43c)){eax=O;edx=0x44440000u;ecx=0xabcd1111u;w32(0,OWNER+0x1a4,0x5eed5eedu);}
    else if(f==VA(0x402188)){w32(0,VA(0x6cbee4),COUNTER2);}
    else if(f==VA(0x40ffc8)){eax=0x0bad0bedu;edx=0x00000007u;ecx=0x12345678u;}
    else CHECK(0);
    r->eax=eax;r->edx=edx;r->ecx=ecx;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void setup(void){
    memset(arena,0,sizeof(arena));ncalls=nobserved=0;
    w32(0,VA(0x6cbee4),COUNTER);w32(0,COUNTER,100);w32(0,COUNTER2,200);
}
int main(void){
    es2_weapon_graph g={IMAGE,0,0,0,0,call,0,observe,0};
    uint32_t ecx=0;
    /* proj_record: base + 24h * s16(i), EDX the entry EDX, nothing written. */
    setup();w32(0,VA(0x4a98fc),OBJECTS+0x1000u);
    uint64_t v=es2_proj_record_registers(&memory,IMAGE,0xabcd0002u,entry);
    CHECK((uint32_t)v==OBJECTS+0x1048u&&(uint32_t)(v>>32)==entry[1]);
    v=es2_proj_record_registers(&memory,IMAGE,0x0000ffffu,entry);CHECK((uint32_t)v==OBJECTS+0x1000u-0x24u);
    v=es2_proj_record_registers(&memory,IMAGE,0x8000u,entry);CHECK((uint32_t)v==OBJECTS+0x1000u-0x24u*0x8000u);
    /* damage_words: the low words, AX/DX keep the entry high words. */
    setup();
    v=es2_damage_words_registers(&memory,IMAGE,0xdead0102u,0xbeef0304u,entry);
    CHECK((uint32_t)v==0x11110102u&&(uint32_t)(v>>32)==0x33330304u);
    CHECK(r16(0,VA(0x4a95f2))==0x0102&&r16(0,VA(0x4a95f4))==0x0304&&r32(0,VA(0x4a95f6))==0);
    /* pitch_point: (dx, dy, dz) with EAX = dz, EDX = dy, ECX = dx, then
     * (distance, dz) with dist2d_delta's registers. */
    setup();
    w32(0,P,10);w32(0,P+4,20);w32(0,P+8,5);w32(0,Q,3);w32(0,Q+4,30);w32(0,Q+8,9);
    v=es2_pitch_point_registers(&memory,&g,P,Q,entry,&ecx);
    CHECK((uint32_t)v==0x77771234u&&(uint32_t)(v>>32)==0x88885678u&&ecx==0x99990001u);
    CHECK(ncalls==2&&calls[0]==VA(0x492a9a)&&calls[1]==VA(0x49290c)&&counts[0]==3&&counts[1]==2);
    CHECK(args[0][0]==7&&args[0][1]==(uint32_t)-10&&args[0][2]==(uint32_t)-4);
    CHECK(regs[0][0]==(uint32_t)-4&&regs[0][1]==(uint32_t)-10&&regs[0][2]==7);
    CHECK(args[1][0]==0x00c0ffeeu&&args[1][1]==(uint32_t)-4&&regs[1][0]==0x00c0ffeeu&&regs[1][1]==0x2001u&&regs[1][2]==0x3001u);
    CHECK(nobserved==2&&observed[0]==VA(0x492a9a)&&observed[1]==VA(0x49290c));
    /* bullet_charge: type 9 locks [W+1A4h] as read after bullet_create. */
    setup();
    v=es2_bullet_charge_registers(&memory,&g,0x77770009u,0x4a9854u,0x4a9860u,OWNER,0x1234abcdu,entry,&ecx);
    CHECK((uint32_t)v==O&&(uint32_t)(v>>32)==0x5eed5eedu&&ecx==0xabcdabcdu);
    CHECK(ncalls==1&&calls[0]==VA(0x40b43c)&&counts[0]==4&&args[0][0]==0x77770009u&&args[0][1]==0x4a9854u);
    CHECK(args[0][2]==0x4a9860u&&args[0][3]==OWNER&&regs[0][0]==0x4a9860u&&regs[0][1]==0x4a9854u&&regs[0][2]==entry[2]);
    CHECK(r16(0,O+0x56)==0xabcd&&r32(0,O+0x5b)==0x5eed5eedu);
    /* Other types keep bullet_create's EDX and O+5Bh. */
    setup();w32(0,O+0x5b,0xfeedfeedu);
    v=es2_bullet_charge_registers(&memory,&g,0x00090008u,1,2,OWNER,0x0000ffffu,entry,NULL);
    CHECK((uint32_t)v==O&&(uint32_t)(v>>32)==0x44440000u&&r16(0,O+0x56)==0xffff&&r32(0,O+0x5b)==0xfeedfeedu);
    /* rocket_init: shape of definition s16(n) = -1, object_init with the
     * original's registers, the vtables, def_lookup(0, n), the counters
     * through the reread pointer (object_init moved it). */
    setup();w32(0,VA(0x4a96d0),DEFS+0x40);w16(0,DEFS+0x40-14,3);w32(0,VA(0x4a96d8),SHAPES);w32(0,SHAPES+12,0xdeadbeefu);
    v=es2_rocket_init_registers(&memory,&g,O,0x5678ffffu,entry,&ecx);
    CHECK((uint32_t)v==O&&(uint32_t)(v>>32)==7&&ecx==COUNTER2);
    CHECK(ncalls==2&&calls[0]==VA(0x402188)&&calls[1]==VA(0x40ffc8));
    CHECK(args[0][0]==O&&args[0][1]==0xdeadbeefu&&regs[0][0]==0xdeadbeefu&&regs[0][1]==SHAPES&&regs[0][2]==DEFS+0x40);
    CHECK(args[1][0]==0&&args[1][1]==0x5678ffffu&&regs[1][0]==COUNTER2&&regs[1][1]==0&&regs[1][2]==0);
    CHECK(r32(0,O)==VA(0x498448)&&r16(0,O+4)==3&&arena[O+0x41-BASE]==0xff&&r32(0,O+0x4e)==0x0bad0bedu&&r32(0,O+0x56)==0);
    CHECK(r32(0,COUNTER)==100&&r32(0,COUNTER2)==202);
    printf("PASS: weapon_leaves %u checks\n",checks);
    return 0;
}
