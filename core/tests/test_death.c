#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "es2/core/death.h"
/* Synthetic state for the core (death_sequence 45fa84). The image
 * base is placed so that the class tables 49f4f4.., the view block 4d24bc
 * and the countdown 4d2a58 fall inside the arena. The callees are small
 * models that log their address, arguments and entry registers. All
 * values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x9f000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x38000u)
static uint8_t arena[0x39000];static unsigned checks,ncalls;
static uint32_t calls[16],args[16][4],regs[16][3],timer_result;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"death line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<16);calls[ncalls]=f;
    for(unsigned j=0;j<n;++j)args[ncalls][j]=a[j];
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;++ncalls;
    if(f==VA(0x42a3f4))CHECK(n==2);
    else if(f==VA(0x45df34)||f==VA(0x42a4c4))CHECK(n==1);
    else if(f==VA(0x45de30))CHECK(n==0);
    else if(f==VA(0x401c74))CHECK(n==4);
    else if(f==VA(0x467aa8)){CHECK(n==1);r->eax=timer_result;r->edx=0x4400u;r->ecx=0x5500u;return;}
    else CHECK(0);
    r->eax=0xa0000000u+ncalls;r->edx=0xb0000000u+ncalls;r->ecx=0xc0000000u+ncalls;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
#define VIEW VA(0x4d24bc)
static void setup(uint32_t player,uint32_t camera,uint16_t class,uint16_t mode){
    uint32_t def=OBJECTS+0x400;
    w32(0,VIEW+0x2a,player);w32(0,VIEW+0x2e,camera);w32(0,VIEW+4,0xd15b1a70u);w16(0,VIEW+0x32,mode);
    w32(0,player+0x1f2,def);w16(0,def+0x50,class);
    w32(0,VA(0x4d2a68),0);
}
int main(void){
    es2_weapon_graph g={IMAGE,0,0,0,0,call,0,0,0};
    uint32_t player=OBJECTS,camera=OBJECTS+0x200;
    /* The class tables as shipped (20000/6000 ms, 32h, -1Eh/1Eh, 0/100h,
     * 1F40h/E0C0h, 0/0, 9C4h/1B58h). */
    w32(0,VA(0x49f4f4),20000);w32(0,VA(0x49f4f8),6000);
    w16(0,VA(0x49f4fc),0x32);w16(0,VA(0x49f4fe),0x32);w16(0,VA(0x49f500),0xffe2);w16(0,VA(0x49f502),0x1e);
    w16(0,VA(0x49f504),0);w16(0,VA(0x49f506),0x100);w16(0,VA(0x49f508),0x1f40);w16(0,VA(0x49f50a),0xe0c0);
    w16(0,VA(0x49f50c),0);w16(0,VA(0x49f50e),0);w32(0,VA(0x49f510),0x9c4);w32(0,VA(0x49f514),0x1b58);
    /* First call, cockpit view, class 1, unlocked camera. */
    setup(player,camera,1,1);w32(0,camera+0x4a,0);timer_result=5;
    uint64_t v=es2_death_sequence_registers(&memory,&g,entry);
    CHECK((uint32_t)v==0);
    CHECK(r32(0,VA(0x4d2a68))==1&&r32(0,VA(0x4d2a59))==6000&&r32(0,VIEW+0x60)==player);
    CHECK(r16(0,VIEW+0x64)==2&&r16(0,VIEW+0x34)==1&&r16(0,VIEW+0x5e)==0);
    CHECK(r32(0,camera+0x26)==0xe0c0u&&r16(0,camera+0x2a)==0&&r16(0,camera+0x22)==0x1b58);
    CHECK(r32(0,camera+0x4a)==1);
    CHECK(ncalls==5&&calls[0]==VA(0x45df34)&&calls[1]==VA(0x42a4c4)&&calls[2]==VA(0x45de30));
    CHECK(calls[3]==VA(0x401c74)&&calls[4]==VA(0x467aa8));
    CHECK(args[0][0]==player&&regs[0][0]==6000&&regs[0][1]==player&&regs[0][2]==player);
    CHECK(args[1][0]==0xd15b1a70u&&regs[1][0]==0xd15b1a70u&&regs[1][1]==0xb0000001u&&regs[1][2]==player);
    CHECK(regs[2][0]==0xa0000002u&&regs[2][2]==0xd15b1a70u);
    /* The camera arguments carry ECX's high word from the view update. */
    CHECK(args[3][0]==camera&&args[3][1]==0xc0000032u&&args[3][2]==0x1e&&args[3][3]==0xc0000100u);
    CHECK(regs[3][0]==camera&&regs[3][1]==0x1e&&regs[3][2]==0xc0000032u);
    CHECK(args[4][0]==VA(0x4d2a58)&&regs[4][0]==camera&&regs[4][1]==0xb0000004u);
    /* Later calls: no setup, entry ECX's high word, the countdown expired. */
    ncalls=0;timer_result=0;w32(0,camera+0x26,7);
    v=es2_death_sequence_registers(&memory,&g,entry);
    CHECK((uint32_t)v==1&&ncalls==2&&r32(0,camera+0x26)==7);
    CHECK(args[0][1]==0x55550032u&&args[0][2]==0x1e&&args[0][3]==0x55550100u&&regs[0][2]==0x55550032u);
    /* Outside view (mode 2), class 0, locked camera: display request 2,
     * view request 3, attachment untouched. */
    setup(player,camera,0,2);w32(0,camera+0x4a,9);w32(0,camera+0x26,0x77);w16(0,camera+0x22,0x66);
    w16(0,VIEW+0x64,0);ncalls=0;timer_result=1;
    v=es2_death_sequence_registers(&memory,&g,entry);
    CHECK((uint32_t)v==0&&r32(0,VA(0x4d2a59))==20000&&r16(0,VIEW+0x5e)==3&&r16(0,VIEW+0x64)==0);
    CHECK(calls[0]==VA(0x42a3f4)&&args[0][0]==0xd15b1a70u&&args[0][1]==2);
    CHECK(regs[0][0]==20000&&regs[0][1]==0xd15b1a70u&&regs[0][2]==entry[2]);
    CHECK(regs[1][1]==0xb0000001u&&regs[1][2]==0xc0000001u);
    CHECK(r32(0,camera+0x26)==0x77&&r16(0,camera+0x22)==0x66&&r32(0,camera+0x4a)==1);
    CHECK(args[3][2]==0xffe2u);
    /* A negative class reads the neighbours below the tables. */
    setup(player,camera,0xffff,1);ncalls=0;w32(0,VA(0x49f4f0),0x1234);
    es2_death_sequence_registers(&memory,&g,entry);
    CHECK(r32(0,VA(0x4d2a59))==0x1234);
    printf("PASS: death %u checks\n",checks);
    return 0;
}
