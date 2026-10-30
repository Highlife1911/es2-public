#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "es2/core/actor_virtuals.h"
/* Synthetic state for the core (eleven actor virtuals). The image base
 * is placed so that 497000..4d3000 (the node record 4a95cc and its guard
 * byte 497424) falls inside the arena; the objects follow. The game callees
 * are small models that log their address, slot, arguments and entry
 * registers and return chosen registers. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x3c000u)
#define ACTOR OBJECTS
#define CONFIG (OBJECTS+0x400u)
#define PARTS (OBJECTS+0x600u)
#define RECORDS (OBJECTS+0x800u)
#define SHAPE (OBJECTS+0xc00u)
#define DETAIL (OBJECTS+0xd00u)
#define TABLE (OBJECTS+0xd80u)
#define NODE (OBJECTS+0xe00u)
#define OTHER (OBJECTS+0x1000u)
#define OUT (OBJECTS+0x1800u)
static uint8_t arena[0x3e000];static unsigned checks,ncalls,nnotes;
static uint32_t calls[4],args[4][3],regs[4][3],notes[4],slots[4];
static es2_weapon_registers returns[4];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"actor_virtuals line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<4&&n<=3);calls[ncalls]=f;
    for(unsigned i=0;i<n;++i)args[ncalls][i]=a[i];
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;*r=returns[ncalls];++ncalls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(nnotes<4);slots[nnotes]=slot;notes[nnotes++]=f;}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,0,0,0,0,call,0,observe,0};return g;}
static void setup(void){
    for(unsigned i=0;i<0x2000;++i)arena[0x3c000+i]=(uint8_t)(0x5a^i);
    w32(0,ACTOR+0x1f2,CONFIG);ncalls=nnotes=0;
    for(unsigned i=0;i<4;++i){returns[i].eax=0xe0+i;returns[i].edx=0xd0+i;returns[i].ecx=0xc0+i;}
}
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
int main(void){
    const es2_weapon_graph g=graph();const uint32_t e[3]={0x11112222u,0x33334444u,0x55556666u};
    /* base_aim_node: guard raised once, memset with the entry registers,
     * words 4000h over memset's high word, +1Ch = s16(C+2Ch). */
    setup();w8(VA(0x497424),0);w16(0,CONFIG+0x2c,0xfffe);returns[0].eax=0xabcd0000u;
    uint64_t v=es2_base_aim_node_registers(&memory,&g,ACTOR,e);
    CHECK(v==pair(VA(0x4a95cc),CONFIG)&&r8(VA(0x497424))==1&&ncalls==1&&calls[0]==VA(0x49681a));
    CHECK(args[0][0]==VA(0x4a95cc)&&args[0][1]==0&&args[0][2]==0x20&&regs[0][0]==e[0]&&regs[0][1]==e[1]&&regs[0][2]==e[2]);
    CHECK(r16(0,VA(0x4a95cc))==0x4000&&r16(0,VA(0x4a95d2))==0x4000&&r16(0,VA(0x4a95dc))==0x4000&&r32(0,VA(0x4a95e8))==0xfffffffeu);
    w8(VA(0x497424),7);setup();es2_base_aim_node_registers(&memory,&g,ACTOR,e);CHECK(r8(VA(0x497424))==7);
    /* base_offsets: 411a74 with EAX = first; second+4 = C+2Ch over its ECX. */
    setup();w16(0,CONFIG+0x2c,0x1234);
    v=es2_base_offsets_registers(&memory,&g,ACTOR,OUT,OUT+8,e);
    CHECK(v==pair(0xe0,CONFIG)&&calls[0]==VA(0x411a74)&&args[0][0]==ACTOR&&args[0][1]==OUT&&args[0][2]==OUT+8);
    CHECK(regs[0][0]==OUT&&regs[0][1]==e[1]&&regs[0][2]==e[2]&&r16(0,OUT+12)==0x1234);
    /* base_condition: 3 parts, currents -2, 50, 100; maxima 100, 100, 50. */
    setup();w32(0,ACTOR+0x205,PARTS);w32(0,CONFIG+0x14,RECORDS);w16(0,CONFIG+0x12,3);
    const uint16_t cur[3]={0xfffe,50,100},max[3]={100,100,50};
    for(unsigned i=0;i<3;++i){w16(0,PARTS+11*i,cur[i]);w16(0,RECORDS+30*i,max[i]);}
    uint32_t ops[2];int fault=-1;v=es2_base_condition_registers(&memory,ACTOR,ops,&fault);
    CHECK(!fault&&ops[0]==148u*256u&&ops[1]==250&&v==pair(148*256/250,148*256%250));
    w16(0,PARTS,0xff00);w16(0,PARTS+11,0);w16(0,PARTS+22,0);v=es2_base_condition_registers(&memory,ACTOR,ops,&fault);
    CHECK(!fault&&v==pair((uint32_t)(-256*256/250),(uint32_t)(-256*256%250)));
    w16(0,CONFIG+0x12,0);v=es2_base_condition_registers(&memory,ACTOR,ops,&fault);CHECK(fault==1&&ops[1]==0&&v==0);
    w16(0,CONFIG+0x12,0x8000);v=es2_base_condition_registers(&memory,ACTOR,ops,&fault);CHECK(fault==1&&ops[0]==0);
    /* actor_attacked: contact_share(actor, attacker), EAX/EDX the arguments. */
    setup();v=es2_actor_attacked_registers(&memory,&g,ACTOR,OTHER,e);
    CHECK(v==pair(0xe0,0xd0)&&calls[0]==VA(0x412704)&&args[0][0]==ACTOR&&args[0][1]==OTHER&&regs[0][0]==ACTOR&&regs[0][1]==OTHER&&regs[0][2]==e[2]);
    /* base_part_position: matrix rebuilt while A+32h is 0, then 4801d4. */
    setup();w16(0,ACTOR+0x32,0);w32(0,CONFIG+0x14,RECORDS);
    v=es2_base_part_position_registers(&memory,&g,ACTOR,0x10002,OUT,e);
    CHECK(v==pair(0xe1,0xd1)&&ncalls==2&&calls[0]==VA(0x47ebb8)&&calls[1]==VA(0x4801d4)&&r16(0,ACTOR+0x32)==1);
    CHECK(args[0][0]==ACTOR+0xc&&args[0][1]==ACTOR+0x12&&regs[0][0]==ACTOR+0x12&&regs[0][1]==ACTOR+0xc&&regs[0][2]==e[2]);
    CHECK(args[1][0]==RECORDS+60+0x10&&args[1][1]==ACTOR+0x12&&args[1][2]==OUT&&regs[1][0]==ACTOR+0x12&&regs[1][1]==RECORDS+76&&regs[1][2]==60);
    setup();w16(0,ACTOR+0x32,2);w32(0,CONFIG+0x14,RECORDS+300);
    v=es2_base_part_position_registers(&memory,&g,ACTOR,0xffff,OUT,e);
    CHECK(v==pair(0xe0,0xd0)&&ncalls==1&&args[0][0]==RECORDS+300-30+0x10&&regs[0][2]==(uint32_t)-30);
    /* mech_aim_node: shape slot +20h with (detail, entry ECX high | C+0Ch). */
    setup();w16(0,CONFIG+0xc,0x0007);w32(0,ACTOR+0x34,SHAPE);w32(0,SHAPE+4,DETAIL);w32(0,DETAIL,TABLE);w32(0,TABLE+0x20,0x12345678u);
    w32(0,SHAPE+0x16,0x1000);returns[0].eax=NODE;w16(0,NODE+4,3);
    v=es2_mech_aim_node_registers(&memory,&g,ACTOR,e);
    CHECK(v==pair(0x1000+96,96)&&calls[0]==0x12345678u&&slots[0]==0x20&&args[0][0]==DETAIL&&args[0][1]==0x55550007u);
    CHECK(regs[0][0]==DETAIL&&regs[0][1]==TABLE&&regs[0][2]==0x55550007u);
    setup();w32(0,ACTOR+0x34,SHAPE);w32(0,SHAPE+4,DETAIL);w32(0,DETAIL,TABLE);returns[0].eax=NODE;w16(0,NODE+4,0xffff);
    v=es2_mech_aim_node_registers(&memory,&g,ACTOR,e);CHECK(v==pair(VA(0x6c56a8),0xd0));
    setup();w32(0,ACTOR+0x34,SHAPE);w32(0,SHAPE+4,DETAIL);w32(0,DETAIL,TABLE);returns[0].eax=0;
    v=es2_mech_aim_node_registers(&memory,&g,ACTOR,e);CHECK(v==pair(VA(0x6c56a8),0xd0));
    /* mech_offsets: words of C+64h..C+6Ah; high words of C+2 and first. */
    setup();for(unsigned i=0;i<4;++i)w16(0,CONFIG+0x64+2*i,(uint16_t)(0x100+i));
    v=es2_mech_offsets_registers(&memory,ACTOR,OUT,OUT+8);
    CHECK(r16(0,OUT)==0&&r16(0,OUT+2)==0x100&&r16(0,OUT+4)==0x101&&r16(0,OUT+8)==0&&r16(0,OUT+10)==0x102&&r16(0,OUT+12)==0x103);
    CHECK(v==pair(((CONFIG+2)&0xffff0000u)|0x103,(OUT&0xffff0000u)|0x102));
    /* Overlap: second over first's z; the original writes second+0 first. */
    setup();v=es2_mech_offsets_registers(&memory,ACTOR,OUT,OUT+4);CHECK(r16(0,OUT+4)==r16(0,CONFIG+0x66));
    /* mech_armour: front for [-4000h, 4000h); the caller's flag 0/1 is front. */
    setup();w16(0,ACTOR+0x222,11);w16(0,ACTOR+0x224,22);
    const uint32_t hi=ACTOR&0xffff0000u;
    CHECK(es2_mech_armour_registers(&memory,ACTOR,0)==pair(hi|11,hi|11)&&es2_mech_armour_registers(&memory,ACTOR,1)==pair(hi|11,hi|11));
    CHECK(es2_mech_armour_registers(&memory,ACTOR,0xc000)==pair(hi|11,hi|11)&&es2_mech_armour_registers(&memory,ACTOR,0x3fff)==pair(hi|11,hi|11));
    CHECK(es2_mech_armour_registers(&memory,ACTOR,0x4000)==pair(hi|22,hi|22)&&es2_mech_armour_registers(&memory,ACTOR,0xbfff)==pair(hi|22,hi|22));
    /* mech_target_scale: dword A+2BDh, or multiply_q10(2000, s16(A+28Eh)). */
    setup();w16(0,CONFIG+0x50,1);w32(0,ACTOR+0x2bd,0x77);
    CHECK(es2_mech_target_scale_registers(&memory,&g,ACTOR,e)==pair(0x77,e[1])&&ncalls==0);
    setup();w16(0,CONFIG+0x50,0);w16(0,ACTOR+0x28e,0xfff6);v=es2_mech_target_scale_registers(&memory,&g,ACTOR,e);
    CHECK(v==pair(0xe0,0xd0)&&calls[0]==VA(0x47e0b0)&&args[0][0]==2000&&args[0][1]==(uint32_t)-10&&regs[0][0]==CONFIG&&regs[0][2]==e[2]);
    /* mech_engage_roll: no draw for a target word +1A8h; draws a/d. */
    setup();w16(0,OTHER+0x1a8,1);CHECK(es2_mech_engage_roll_registers(&memory,&g,ACTOR,OTHER,e)==pair(0,e[1])&&ncalls==0);
    setup();w16(0,OTHER+0x1a8,0);w16(0,ACTOR+0x29e,100);w16(0,OTHER+0x29e,50);
    returns[0].eax=0x12340fffu;returns[1].eax=0x56780000u;
    v=es2_mech_engage_roll_registers(&memory,&g,ACTOR,OTHER,e);
    /* a = 3E8h + 100 = 1100, d = 0 + 50: spread 1050 >= 400, d < a: 0. */
    CHECK(v==pair(0,0x56780000u|50)&&ncalls==2&&args[0][0]==VA(0x4d2599)&&regs[0][0]==OTHER&&regs[1][0]==(0x12340000u|1100)&&regs[1][2]==VA(0x4d2599));
    setup();w16(0,OTHER+0x1a8,0);w16(0,ACTOR+0x29e,0);w16(0,OTHER+0x29e,500);returns[0].eax=0;returns[1].eax=0;
    v=es2_mech_engage_roll_registers(&memory,&g,ACTOR,OTHER,e);CHECK(v==pair(2,500));
    setup();w16(0,OTHER+0x1a8,0);w16(0,ACTOR+0x29e,10);w16(0,OTHER+0x29e,0);returns[0].eax=0x0100;returns[1].eax=0x0100;
    v=es2_mech_engage_roll_registers(&memory,&g,ACTOR,OTHER,e);CHECK(v==pair(1,0x100));
    setup();w16(0,OTHER+0x1a8,0);w16(0,ACTOR+0x29e,0x8000);w16(0,OTHER+0x29e,0);returns[0].eax=0;returns[1].eax=0;
    /* a = 8000h: the difference 8000h counts as 7FFFh; d = 0 > a = -8000h: 2. */
    v=es2_mech_engage_roll_registers(&memory,&g,ACTOR,OTHER,e);CHECK(v==pair(2,0));
    /* mech_link_position: copy z, x, y of the link's position; byte +ADh. */
    setup();w32(0,ACTOR+0x1a4,0);CHECK(es2_mech_link_position_registers(&memory,ACTOR,e)==pair(ACTOR,0));
    setup();w32(0,ACTOR+0x1a4,OTHER);w32(0,OTHER+0x26,1);w32(0,OTHER+0x2a,2);w32(0,OTHER+0x2e,3);w8(ACTOR+0xad,0);
    v=es2_mech_link_position_registers(&memory,ACTOR,e);
    CHECK(v==pair(ACTOR+0x92,ACTOR+0x31e)&&r32(0,ACTOR+0x31e)==1&&r32(0,ACTOR+0x322)==2&&r32(0,ACTOR+0x326)==3&&r8(ACTOR+0xad)==1);
    printf("actor_virtuals: %u checks\n",checks);
    return 0;
}
