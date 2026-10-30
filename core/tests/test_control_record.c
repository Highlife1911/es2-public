#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/control_record.h"
/* Synthetic state for the core (control_record 45a810). The image
 * base is placed so that the globals 49ea90..4d3b65 fall inside the
 * arena; the callees are small models that log their address, arguments
 * and entry registers. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x9e000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x38000u)
#define FRAME (BASE+0x3c000u)
#define REC VA(0x4d22c6)
#define DEV VA(0x4d23f6)
static uint8_t arena[0x3d000];static unsigned checks,ncalls;
static uint32_t calls[64],args[64][6],regs[64][3];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"control_record line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
/* Model state: the key words the reader delivers, the joystick object,
 * the tape bytes. */
static uint16_t keys[4];static unsigned nkeys;static uint32_t joystick,tape_ok;
static uint8_t tape[64];static unsigned tape_at;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<64);calls[ncalls]=f;
    for(unsigned j=0;j<n;++j)args[ncalls][j]=a[j];
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;++ncalls;
    uint32_t eax=0xa0000000u+ncalls,edx=0xb0000000u+ncalls,ecx=0xc0000000u+ncalls;
    if(f==VA(0x49681a)){CHECK(n==3);memset(host(0,a[0]),(int)a[1],a[2]);eax=a[0];}
    else if(f==VA(0x45baa8)){CHECK(n==0);eax=DEV;}
    else if(f==VA(0x45bbc4)){CHECK(n==2&&a[1]==1);if(nkeys){w16(0,a[0],keys[--nkeys]);eax=1;}else eax=0x100;}
    else if(f==VA(0x45c524)){CHECK(n==1&&a[0]==3);eax=joystick;}
    else if(f==VA(0x477904)){CHECK(n==0);eax=OBJECTS;}
    else if(f==VA(0x49684a)){CHECK(n==4);unsigned len=a[1]*a[2];CHECK(tape_at+len<=sizeof(tape));
        memcpy(host(0,a[0]),tape+tape_at,len);tape_at+=len;eax=tape_ok;}
    else if(f==VA(0x452d40)){CHECK(n==0);eax=OBJECTS+0x100;}
    else if(f==VA(0x496802)){CHECK(n==2);eax=0x5000;}
    else if(f==VA(0x492b90)){CHECK(n==2);eax=OBJECTS+0x200;}
    r->eax=eax;r->edx=edx;r->ecx=ecx;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
/* Pointer tables as the input initialisation 45a6a7.. leaves them. */
static void setup(void){
    memset(arena,0,sizeof(arena));ncalls=0;nkeys=0;joystick=0;tape_at=0;tape_ok=1;
    w32(0,VA(0x4d2390),DEV);w16(0,VA(0x49eab8),0xffff);
    for(unsigned i=0;i<4;++i)w32(0,VA(0x4d2310)+4*i,REC+14+2*i);
    for(unsigned i=0;i<8;++i)w32(0,VA(0x4d2320)+4*i,REC+0x16+i);
    for(unsigned i=0;i<4;++i)w32(0,VA(0x4d2340)+4*i,VA(0x4d23c1)+4*i);
    w32(0,VA(0x4d2350),DEV+0xd);w32(0,VA(0x4d2354),DEV+0x11);w32(0,VA(0x4d2358),DEV+0x15);w32(0,VA(0x4d235c),DEV+0x19);
    for(unsigned i=0;i<4;++i)w32(0,VA(0x4d2360)+4*i,VA(0x4d23d1)+i);
    for(unsigned i=0;i<8;++i)w32(0,VA(0x4d2370)+4*i,DEV+0x1d+i);
}
static uint64_t run(uint32_t *ecx){
    es2_weapon_graph g={IMAGE,FRAME,0,0,0,call,0,0,0};
    return es2_control_record_registers(&memory,&g,ecx,entry);
}
/* flight_key 45a324 on a synthetic list (not the shipped codes):
 * entries 0..14 are 60h+j, entries 15..29 are E0h+j-15 except where a
 * test places a code. */
#define FK_D VA(0x4d2394)
#define FK_U VA(0x4d23a3)
#define FK_S VA(0x4d23b2)
static void fk_setup(void){
    memset(arena,0,sizeof(arena));
    for(unsigned j=0;j<30;++j)w8(VA(0x49eb0b)+j,(uint8_t)(j<15?0x60+j:0xe0+j-15));
    w8(VA(0x49eaa5),1);w8(VA(0x4d2571),1);w16(0,VA(0x4d23d6),0x5555);
}
static uint64_t fk(uint32_t code){return es2_flight_key_registers(&memory,IMAGE,code);}
static void flight_key_tests(void){
    uint64_t v;
    /* Demo word: the quit word, nothing else. */
    fk_setup();w16(0,VA(0x4d2530),7);v=fk(0x12340263u);
    CHECK(v==((uint64_t)0x12340263u<<32|2u)&&r16(0,VA(0x4d2532))==1&&r8(FK_D+3)==0&&r16(0,VA(0x4d23d6))==0x5555);
    /* Capture off: passes with the whole code in EDX. */
    fk_setup();w8(VA(0x49eaa5),0);v=fk(0x2e3u);
    CHECK(v==((uint64_t)0x2e3u<<32|2u)&&r8(FK_U+3)==0&&r16(0,VA(0x4d2532))==0);
    /* Key-down with Alt: down set, released cleared, Alt word, consumed;
     * EDX keeps the high word and bits 0..6. */
    fk_setup();w8(FK_U+3,1);v=fk(0xabcd0263u);
    CHECK(v==((uint64_t)0xabcd0063u<<32|1u)&&r8(FK_D+3)==1&&r8(FK_U+3)==0&&r16(0,VA(0x4d23d6))==0x200);
    /* Key-up before a frame saw it: released set, down kept. */
    fk_setup();w8(FK_D+5,1);v=fk(0xe5u);
    CHECK(v==((uint64_t)0x65u<<32|1u)&&r8(FK_D+5)==1&&r8(FK_U+5)==1&&r16(0,VA(0x4d23d6))==0);
    /* Key-up after it was seen: all three cleared. */
    fk_setup();w8(FK_D+5,1);w8(FK_U+5,1);w8(FK_S+5,9);v=fk(0x8e5u);
    CHECK((uint32_t)v==1&&r8(FK_D+5)==0&&r8(FK_U+5)==0&&r8(FK_S+5)==0&&r16(0,VA(0x4d23d6))==0);
    /* Num- and Num+ pass untouched while 4d2571 is 0, are keys otherwise. */
    fk_setup();w8(VA(0x49eb0b)+12,0x4e);w8(VA(0x4d2571),0);v=fk(0x4eu);
    CHECK(v==((uint64_t)0x4eu<<32|2u)&&r8(FK_D+12)==0&&r16(0,VA(0x4d23d6))==0x5555);
    w8(VA(0x4d2571),1);v=fk(0x4eu);CHECK((uint32_t)v==1&&r8(FK_D+12)==1);
    /* Not in the list: passes, nothing written. */
    fk_setup();v=fk(0x5fu);CHECK(v==((uint64_t)0x5fu<<32|2u)&&r16(0,VA(0x4d23d6))==0x5555);
    /* Space and Enter keep the state and pass. */
    fk_setup();w8(VA(0x49eb0b)+14,0x39);v=fk(0x39u);CHECK((uint32_t)v==2&&r8(FK_D+14)==1&&r16(0,VA(0x4d23d6))==0);
    fk_setup();w8(VA(0x49eb0b)+2,0x1c);v=fk(0x9cu);CHECK((uint32_t)v==2&&r8(FK_U+2)==1);
    /* The -SPRUNKNOWN flag passes Ctrl and Alt keys, not Shift. */
    fk_setup();w32(0,VA(0x49ef38),1);v=fk(0x461u);CHECK((uint32_t)v==2&&r8(FK_D+1)==1);
    v=fk(0x861u);CHECK((uint32_t)v==1);
    /* Folding: entry 16 is key 1, entry 15 passes. */
    fk_setup();w8(VA(0x49eb0b)+16,0x30);v=fk(0x30u);CHECK((uint32_t)v==1&&r8(FK_D+1)==1);
    fk_setup();w8(VA(0x49eb0b)+15,0x31);v=fk(0x31u);CHECK((uint32_t)v==2&&r16(0,VA(0x4d23d6))==0x5555);
}
/* command_queue 45a498. */
static uint64_t cq(uint32_t code,uint32_t edx){return es2_command_queue_registers(&memory,IMAGE,code,edx);}
static void command_queue_tests(void){
    uint64_t v;
    /* Capture byte clear: 2, EDX as it came, nothing written. */
    memset(arena,0,sizeof(arena));w32(0,VA(0x49eaa0),3);v=cq(0x1234u,0xabcd0099u);
    CHECK(v==((uint64_t)0xabcd0099u<<32|2u)&&r32(0,VA(0x49eaa0))==3&&r16(0,VA(0x4d20c4)+6)==0);
    /* Capture set: the low word of the code is appended, EDX keeps its high word. */
    w8(VA(0x49eaa4),0x80);v=cq(0xfedc0e8fu,0xabcd0099u);
    CHECK(v==((uint64_t)0xabcd0e8fu<<32|1u)&&r32(0,VA(0x49eaa0))==4&&r16(0,VA(0x4d20c4)+6)==0x0e8f&&r16(0,VA(0x4d20c4)+8)==0);
    v=cq(0x28u,0);CHECK((uint32_t)v==1&&r32(0,VA(0x49eaa0))==5&&r16(0,VA(0x4d20c4)+8)==0x28);
    /* No bound: an index past the queue writes into the next globals (the
     * control record 4d22c6 follows after 257 words). */
    w32(0,VA(0x49eaa0),0x101);v=cq(0x77u,0);
    CHECK(r16(0,VA(0x4d22c6))==0x77&&r32(0,VA(0x49eaa0))==0x102);
    /* A negative index wraps the address; the store may land on the count
     * itself and the increment rereads it (49eaa0 = 4d20c4 + 2 * -19b12h). */
    w32(0,VA(0x49eaa0),0xfffe64eeu);v=cq(0x5a5au,0);
    CHECK(r32(0,VA(0x49eaa0))==0xfffe5a5bu);
}
int main(void){
    uint32_t ecx;
    flight_key_tests();
    command_queue_tests();
    /* Keyboard only: a key word with an empty low byte is replaced by the
     * reader; the keyboard-axis block feeds the axes, the device buttons
     * the button bytes. */
    setup();w16(0,DEV,0x0200);keys[0]=0x0141;nkeys=1;
    w32(0,VA(0x4d23c1),0x1234);w32(0,VA(0x4d23cd),0xfff0);w8(DEV+0x1f,1);w8(VA(0x4d23d1),1);
    w32(0,DEV+2,0x101);w32(0,DEV+6,0x202);w8(DEV+0xb,1);w8(DEV+0x28,7);w32(0,VA(0x49eaa0),2);
    w8(VA(0x4d1f38)+3,1); /* hat mapping 1: the hat bytes are kept */
    w16(0,VA(0x4d20c4),0x31);w16(0,VA(0x4d20c6),0x32);
    uint64_t v=run(&ecx);
    CHECK((uint32_t)v==REC&&(uint32_t)(v>>32)==REC+0x10&&ecx==((DEV+2)&0xffffff00u)+7);
    CHECK(calls[0]==VA(0x464c78)&&calls[1]==VA(0x49681a)&&calls[2]==VA(0x45baa8)&&calls[3]==VA(0x45bbc4));
    CHECK(calls[4]==VA(0x45c524)&&calls[5]==VA(0x45a4cc)&&calls[6]==VA(0x45feb8)&&calls[7]==VA(0x45feb8));
    CHECK(calls[8]==VA(0x452d40)&&ncalls==9);
    CHECK(regs[0][0]==entry[0]&&regs[0][1]==entry[1]&&regs[0][2]==entry[2]);
    CHECK(args[1][0]==REC&&args[1][1]==0&&args[1][2]==0x30&&regs[1][0]==0xa0000001u);
    /* The key read: EAX the device block, DX the key word. */
    CHECK(args[3][0]==FRAME+0xa&&regs[3][0]==DEV&&regs[3][1]==0xb0000200u&&regs[3][2]==FRAME+0xa);
    CHECK(regs[4][0]==0&&regs[4][1]==0&&regs[4][2]==0);
    CHECK(args[5][0]==VA(0x4d23c1)&&args[5][1]==0&&regs[5][0]==0&&regs[5][1]==0&&regs[5][2]==3);
    /* Queued keys: DX the masked word over the high half of EDX; ECX the
     * popped argument. */
    CHECK(args[6][0]==0xb0000031u&&args[7][0]==0xb0000032u&&regs[6][0]==0xa000ffffu&&regs[6][2]==DEV+0x1d);
    CHECK(regs[7][2]==args[6][0]&&regs[8][2]==args[7][0]);
    CHECK(r16(0,REC)==0x141&&r16(0,DEV)==0x141&&r16(0,REC+0xe)==0x1234&&r16(0,REC+0x14)==0xfff0);
    CHECK(r8(REC+0x16)==1&&r8(REC+0x17)==0&&r8(REC+0x18)==1&&r8(REC+0xd)==1&&r8(REC+0xb)==1&&r8(REC+0x21)==7);
    CHECK(r32(0,REC+2)==0x101&&r32(0,REC+6)==0x202&&r32(0,REC+0x22)==REC+0xe&&r32(0,REC+0x26)==REC+0x10);
    CHECK(r32(0,VA(0x49eaa0))==0);
    /* Joystick mapping 1 (axes from device +0Dh/+11h), throttle and rudder
     * on with mapping 1: the second sources +15h/+19h win; mapping byte 1
     * at 4d1f4b consumes button 2 into +0Dh. */
    setup();joystick=0x77;w16(0,OBJECTS,1);w8(OBJECTS+4,1);w8(OBJECTS+5,1);
    w8(VA(0x4d2577),4);w8(VA(0x4d1f38)+4,1);w8(VA(0x4d1f38)+5,1);w8(VA(0x4d1f38)+6,1);
    w32(0,DEV+0xd,0x55);w32(0,DEV+0x11,0x66);w32(0,DEV+0x15,0x77);w32(0,DEV+0x19,0x88);
    w8(VA(0x4d1f38)+0x13,1);w8(DEV+0x1f,9);
    v=run(&ecx);
    CHECK(calls[4]==VA(0x477904)&&regs[4][0]==0x77&&regs[4][2]==3&&calls[5]==VA(0x45a4cc)&&args[5][1]==1);
    CHECK(r16(0,REC+0xe)==0x88&&r16(0,REC+0x10)==0x77&&r16(0,REC+0x12)==0&&r16(0,REC+0x14)==0);
    CHECK(r8(REC+0xd)==9&&r8(DEV+0x1f)==9&&r8(REC+0x18)==0&&r32(0,REC+0x22)==REC+0xe);
    /* Playback: one entry decoded into the record, the time scale +2Eh,
     * the second button set from byte +0Dh of the entry. */
    setup();w16(0,VA(0x4d24d6),1);w32(0,VA(0x4d24e2),0x6000);w16(0,DEV,0x55);
    {uint8_t e[16]={0x41,0,1,0,2,0,3,0,4,0,0x40,0,0x81,0x03,1,0};memcpy(tape,e,16);}
    memset(tape+16,0,8);
    v=run(&ecx);
    CHECK(calls[3]==VA(0x49684a)&&args[3][0]==FRAME+0x58&&args[3][3]==0x6000&&regs[3][0]==FRAME+0x58&&regs[3][2]==0x6000);
    CHECK(calls[6]==VA(0x45fe6c)&&(args[6][0]&0xffff)==0x55&&calls[7]==VA(0x49684a)&&args[7][0]==VA(0x4d20c4));
    CHECK(r16(0,REC)==0x41&&r16(0,REC+0xe)==1&&r16(0,REC+0x14)==4&&r16(0,REC+0x2e)==0x40);
    CHECK(r8(REC+0x16)==1&&r8(REC+0x17)==0&&r8(REC+0x21)==1&&r8(REC+0xd)==1);
    CHECK(r8(FRAME+4)==1&&r8(FRAME+5)==1&&r8(FRAME+6)==0&&r16(0,VA(0x4d24d6))==1);
    /* A short read ends playback and clears the key. */
    setup();w16(0,VA(0x4d24d6),1);tape_ok=0;w16(0,DEV,0x55);
    v=run(&ecx);
    CHECK(r16(0,VA(0x4d24d6))==0&&r8(VA(0x4d1dd6))==1&&r16(0,DEV)==0&&calls[7]==VA(0x496838));
    /* Recording: the entry written from the record and the button set. */
    setup();w16(0,VA(0x4d24d8),1);w16(0,VA(0x4d3b64),47);w16(0,DEV,0x51);w8(DEV+0x1d,1);w8(DEV+0x24,1);
    w8(VA(0x4d23d4),1);
    v=run(&ecx);
    unsigned i=0;while(i<ncalls&&calls[i]!=VA(0x496832))++i;
    CHECK(i<ncalls&&args[i][0]==FRAME+0xb8&&args[i][2]==0x10&&regs[i][0]==47&&regs[i][2]==0);
    CHECK(r16(0,FRAME+0xb8)==0x51&&r16(0,FRAME+0xc2)==47&&r32(0,FRAME+0xc4)==(0x01u|0x08u|0x100u|0x8000u));
    CHECK(calls[ncalls-1]==VA(0x496838)&&args[ncalls-1][0]==0x5000&&ecx==0x5000);
    printf("PASS: control_record %u checks\n",checks);
    return 0;
}
