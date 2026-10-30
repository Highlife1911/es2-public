#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/keyboard.h"
/* Synthetic state for the core (key_message 477bec). The arena
 * covers the image range from the key table 4a10ec to the device pointer
 * 6c5eb8; the key table holds a few synthetic codes, not the shipped
 * table. The callees are models that log their address, argument and
 * entry registers. */
#define IMAGE UINT32_C(0x90000000)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define BASE VA(0x4a1000)
#define LISTS VA(0x6c6000)
#define DEVICE_WORD VA(0x6c6100)
#define CALLBACK_A VA(0x450000)
#define CALLBACK_B VA(0x450100)
static uint8_t arena[0x226000];static unsigned checks,ncalls;
static uint32_t calls[16],args[16],regs[16][3],down_mask,callback_results[2];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"keyboard line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
/* GetKeyState returns a SHORT with bit 15 for keys in down_mask (bit vk-10h)
 * and junk in the high word; the callbacks return callback_results. */
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<16);calls[ncalls]=f;args[ncalls]=n?a[0]:0;
    regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;++ncalls;
    r->edx=0xd0000000u+ncalls;r->ecx=0xc0000000u+ncalls;
    if(f==VA(0x49695e)){CHECK(n==1&&a[0]>=0x10u&&a[0]<=0x12u);r->eax=0x5a5a0000u|(down_mask>>(a[0]-0x10u)&1u?0x8001u:0x7fffu);}
    else if(f==VA(0x4669b4)){CHECK(n==0);r->eax=0xa0000000u;}
    else if(f==CALLBACK_A){CHECK(n==1);r->eax=callback_results[0];}
    else if(f==CALLBACK_B){CHECK(n==1);r->eax=callback_results[1];}
    else CHECK(0);
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,0,0,0,0,call,0,0,0};return g;}
static uint64_t run(uint32_t message,uint32_t key){
    es2_weapon_graph g=graph();ncalls=0;
    return es2_key_message_registers(&memory,&g,0x1234u,message,key,0x00480001u,entry);
}
static void reset(uint32_t write,uint32_t read){
    memset(arena+(VA(0x6bb6b0)-BASE),0,0x280);
    w32(0,VA(0x6bb8b0),write);w32(0,VA(0x6bb8b4),read);
    w32(0,VA(0x6c5eb8),DEVICE_WORD);w32(0,DEVICE_WORD,0x100u);
    down_mask=0;callback_results[0]=callback_results[1]=0;
}
static void hotkey(unsigned i,uint32_t active,uint32_t list,uint32_t callback){
    uint32_t e=VA(0x6bb8b8)+12u*i;w32(0,e,active);w32(0,e+4,list);w32(0,e+8,callback);
}
static void queued(uint32_t write,uint16_t code){
    CHECK(r16(0,VA(0x6bb6b0)+2u*write)==code);CHECK(r32(0,VA(0x6bb8b0))==((write+1u)&0xffu));
    CHECK(r32(0,DEVICE_WORD)==0x102u);
}
static void key_state_calls(unsigned first){
    CHECK(calls[first]==VA(0x49695e)&&args[first]==0x10u);
    CHECK(calls[first+1]==VA(0x49695e)&&args[first+1]==0x12u);
    CHECK(calls[first+2]==VA(0x49695e)&&args[first+2]==0x11u);
}
int main(void){
    /* Synthetic codes: Up 48h, Tab 0Fh, Esc 01h, F10 44h; VK 42h unmapped. */
    w32(0,VA(0x4a10ec)+4u*0x26u,0x48u);w32(0,VA(0x4a10ec)+4u*0x09u,0x0fu);
    w32(0,VA(0x4a10ec)+4u*0x1bu,0x01u);w32(0,VA(0x4a10ec)+4u*0x79u,0x44u);
    w32(0,VA(0x4a10ec)+4u*0xdeu,0x2bu);
    /* Other messages: EAX 0, EDX = wParam, nothing read or called. */
    reset(5,0);
    const uint32_t others[]={0xffu,0x102u,0x103u,0x106u,0u,0x200u,0xffffffffu};
    for(unsigned i=0;i<sizeof(others)/sizeof(others[0]);++i){
        uint64_t v=run(others[i],0x26u);CHECK(v==(uint64_t)0x26u<<32&&ncalls==0);
    }
    CHECK(r32(0,VA(0x6bb8b0))==5&&r32(0,DEVICE_WORD)==0x100u);
    /* Unmapped key (zero entry, or index DFh and above): one GetKeyState(VK_MENU). */
    const uint32_t unmapped[]={0x42u,0xdfu,0x100u,0x80000026u};
    for(unsigned i=0;i<4;++i){
        reset(5,0);uint64_t v=run(0x100u,unmapped[i]);
        CHECK(v==((uint64_t)0xd0000001u<<32)&&ncalls==1&&calls[0]==VA(0x49695e)&&args[0]==0x12u);
        CHECK(regs[0][0]==0x100u&&regs[0][1]==unmapped[i]&&regs[0][2]==0xfffffffeu);
        down_mask=1u<<2;v=run(0x105u,unmapped[i]);CHECK(v==((uint64_t)0xd0000001u<<32|1u)&&regs[0][2]==0xffffffffu);
        CHECK(r32(0,VA(0x6bb8b0))==5);
    }
    /* Highest table index DEh is read. */
    reset(5,0);CHECK((uint32_t)run(0x100u,0xdeu)==0);queued(5,0x2b);
    /* Plain key down: three GetKeyState calls in the order Shift, Alt, Ctrl,
     * code queued at the write index, device bit 1 set; EDX = old index. */
    reset(5,0);uint64_t v=run(0x100u,0x26u);
    CHECK(v==(uint64_t)5u<<32&&ncalls==3);key_state_calls(0);queued(5,0x48);
    CHECK(regs[0][0]==0x100u&&regs[0][1]==0x26u&&regs[0][2]==0xfffffffeu);
    CHECK(regs[1][0]==0x5a5a7fffu&&regs[1][1]==0xd0000001u&&regs[1][2]==0xc0000001u);
    /* Key up (101h and 105h) adds 80h; WM_SYSKEYDOWN starts with ECX = -2. */
    reset(250,0);v=run(0x101u,0x26u);CHECK(v==(uint64_t)250u<<32);queued(250,0xc8);CHECK(regs[0][2]==0xffffffffu);
    reset(255,3);v=run(0x105u,0x26u);CHECK((uint32_t)v==0);queued(255,0xc8);CHECK(r32(0,VA(0x6bb8b0))==0);
    reset(7,0);v=run(0x104u,0x26u);CHECK((uint32_t)v==0&&regs[0][2]==0xfffffffeu);queued(7,0x48);
    /* Modifiers: Shift 800h, Alt 200h, Ctrl 400h. */
    reset(1,0);down_mask=7;v=run(0x100u,0x26u);queued(1,0xe48);
    reset(1,0);down_mask=5;v=run(0x101u,0x26u);queued(1,0xac8);
    /* Alt+Tab, Ctrl+Esc, Alt+Esc (down and up): 4669b4, nothing queued. */
    const uint32_t focus_keys[3][2]={{0x09u,4u},{0x1bu,2u},{0x1bu,4u}};
    for(unsigned i=0;i<3;++i)for(unsigned upkey=0;upkey<2;++upkey){
        reset(9,0);down_mask=focus_keys[i][1];v=run(upkey?0x101u:0x100u,focus_keys[i][0]);
        CHECK(v==((uint64_t)0xd0000004u<<32)&&ncalls==4&&calls[3]==VA(0x4669b4));
        CHECK(regs[3][0]==(focus_keys[i][1]==2u?0x5a5a8001u:0x5a5a7fffu));
        CHECK(r32(0,VA(0x6bb8b0))==9&&r32(0,DEVICE_WORD)==0x100u);
    }
    /* Shift+Alt+Tab is not one of them. */
    reset(9,0);down_mask=5;v=run(0x100u,0x09u);CHECK(ncalls==3);queued(9,0xa0f);
    /* F10 down returns 1 (also with a full ring); F10 up returns 0. */
    reset(3,0);v=run(0x100u,0x79u);CHECK((uint32_t)v==1);queued(3,0x44);
    reset(3,4);v=run(0x100u,0x79u);CHECK((uint32_t)v==1&&r32(0,VA(0x6bb8b0))==3&&r32(0,DEVICE_WORD)==0x100u);
    reset(3,0);v=run(0x101u,0x79u);CHECK((uint32_t)v==0);queued(3,0xc4);
    /* Full ring: nothing written, EDX from the last call. */
    reset(255,0);v=run(0x100u,0x26u);CHECK(v==((uint64_t)0xd0000003u<<32)&&r16(0,VA(0x6bb6b0)+510u)==0);
    CHECK(r32(0,VA(0x6bb8b0))==255&&r32(0,DEVICE_WORD)==0x100u);
    /* The write index is used unmasked: 100h writes over its own low word,
     * then the index becomes 1. */
    reset(0x100u,0x50u);v=run(0x100u,0x26u);CHECK(v==((uint64_t)0x100u<<32)&&r32(0,VA(0x6bb8b0))==1u);
    CHECK(r32(0,DEVICE_WORD)==0x102u);
    /* Hot keys: inactive entries are skipped; a matching byte calls the
     * entry's callback with the whole code; result 1 consumes it. */
    static const uint8_t list0[]={0x10,0x48,0x22,0},list1[]={0xc8,0x48,0},list2[]={0x48,0};
    memcpy(host(0,LISTS),list0,sizeof(list0));memcpy(host(0,LISTS+0x10),list1,sizeof(list1));memcpy(host(0,LISTS+0x20),list2,sizeof(list2));
    reset(5,0);hotkey(0,0,LISTS+0x20,CALLBACK_B);hotkey(3,0x80000000u,LISTS,CALLBACK_A);callback_results[0]=1;down_mask=1;
    v=run(0x100u,0x26u);
    CHECK(v==((uint64_t)0xd0000004u<<32|1u)&&ncalls==4&&calls[3]==CALLBACK_A&&args[3]==0x848u);
    CHECK(regs[3][0]==LISTS+2u&&regs[3][1]==0xd0000048u&&regs[3][2]==0xc0000003u);
    CHECK(r32(0,VA(0x6bb8b0))==5&&r32(0,DEVICE_WORD)==0x100u);
    /* Any other result ends that entry's list; a later entry is offered the
     * code, ECX = the code after the first callback; then it is queued. */
    reset(5,0);hotkey(1,1,LISTS+0x10,CALLBACK_A);hotkey(4,1,LISTS+0x20,CALLBACK_B);callback_results[0]=2;callback_results[1]=0;
    v=run(0x100u,0x26u);
    CHECK(ncalls==5&&calls[3]==CALLBACK_A&&calls[4]==CALLBACK_B&&args[4]==0x48u);
    CHECK(regs[3][0]==LISTS+0x12u&&regs[4][0]==LISTS+0x21u&&regs[4][1]==0xd0000048u&&regs[4][2]==0x48u);
    CHECK(v==((uint64_t)5u<<32));queued(5,0x48);
    /* Key up matches C8h in the first list; a result whose low byte is 1
     * but not the whole dword does not consume. */
    reset(5,0);hotkey(1,1,LISTS+0x10,CALLBACK_A);callback_results[0]=0x101u;
    v=run(0x101u,0x26u);CHECK(ncalls==4&&args[3]==0xc8u&&regs[3][0]==LISTS+0x11u);queued(5,0xc8);
    /* Unmatched lists call nothing. */
    reset(5,0);hotkey(1,1,LISTS+0x10,CALLBACK_A);hotkey(2,1,LISTS,CALLBACK_B);
    v=run(0x100u,0x1bu);CHECK(ncalls==3);queued(5,0x01);
    printf("PASS: keyboard (%u checks)\n",checks);
    return 0;
}
