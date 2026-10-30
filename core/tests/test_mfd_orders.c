#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/mfd_orders.h"
/* Synthetic state for the core (the MFD order path). The image base
 * is placed so that the globals 49b088, 49b0f8, 49dbe0, 4a9b7a, the
 * wingman table 4d03c8 and the order record 4d03d4 fall inside the arena;
 * the MFD gauge G, its menu, the cockpit H, the group, its members and a
 * frame lie above them. A logging call function stands in for every game
 * call (target, arguments, count, entry EAX/EDX/ECX and the frame's
 * EBX/ESI/EDI/return address); it performs memset 49681a, answers dist_gate
 * 49288c from a per-member distance and the member order slot +28h from a
 * per-member answer, and returns fixed registers otherwise. Expected values
 * are worked out by hand from the contract in cockpit.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x49b088u+0x400000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define GAUGE (BASE+0x110000u)
#define MENU (BASE+0x111000u)
#define COCKPIT (BASE+0x112000u)
#define GROUP (BASE+0x113000u)
#define MEMBERS (BASE+0x113100u)
#define MEMBER(i) (BASE+0x114000u+0x400u*(i))
#define VTABLE (BASE+0x118000u)
#define LAMP(i) (BASE+0x119000u+0x100u*(i))
#define POINT (BASE+0x11a000u)
#define ENTRY(i) (BASE+0x11b000u+0x40u*(i))
#define FRAME (BASE+0x11c100u)
static uint8_t arena[0x120000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mfd_orders line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static uint8_t b8(uint32_t a){return arena[a-BASE];}
typedef struct call { uint32_t va,arg[3],count,eax,edx,ecx,ebx,esi,edi,ret; } call;
static call calls[256];static unsigned call_count,observed,slots;
static es2_mfd_frame *active_frame;
static uint32_t distance[16],answer[16];
static unsigned member_of(uint32_t a){return (a-MEMBER(0))/0x400u;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<256);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f>=VTABLE&&f<VTABLE+0x100u?f:f-IMAGE+0x400000u;
    for(unsigned i=0;i<n&&i<3;++i)k->arg[i]=a[i];k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(active_frame){k->ebx=active_frame->call.ebx;k->esi=active_frame->call.esi;k->edi=active_frame->call.edi;k->ret=active_frame->call.ret;}
    if(k->va==0x49681au){for(uint32_t j=0;j<a[2];++j)arena[a[0]+j-BASE]=(uint8_t)a[1];r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    if(k->va==0x49288cu){r->eax=distance[member_of(a[0]-0x26u)];r->edx=0xd0000000u+call_count;r->ecx=0xc0000000u+call_count;return;}
    if(k->va==VTABLE+0x28u){r->eax=answer[member_of(a[0])];r->edx=0xdd00u+call_count;r->ecx=0xcc00u+call_count;return;}
    r->eax=0xa0u+call_count;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;++observed;if(slot)++slots;}
static const es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
static void setup(void){
    memset(arena,0xcc,sizeof(arena));call_count=observed=slots=0;active_frame=NULL;
    w32(0,VA(0x49b088),COCKPIT);w32(0,GAUGE+0xd1,MENU);arena[VA(0x49dbe0)-BASE]=0;
}
static void check_call(unsigned n,uint32_t va,unsigned count,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t eax,uint32_t edx,uint32_t ecx){
    CHECK(n<call_count);const call *k=calls+n;
    CHECK(k->va==va&&k->count==count&&k->arg[0]==a0);
    if(count>1)CHECK(k->arg[1]==a1);
    if(count>2)CHECK(k->arg[2]==a2);
    CHECK(k->eax==eax&&k->edx==edx&&k->ecx==ecx);
}
static void test_leaves(void){
    uint32_t ecx;
    /* menu_code: entry 3, bit 1 clear, then set (code 6). */
    setup();w32(0,MENU+0x32,3);arena[MENU+0x2c+3-BASE]=0xfd;
    uint64_t v=es2_menu_code_registers(&memory,MENU,&ecx);
    CHECK(v==((uint64_t)3<<32|3)&&ecx==3);
    arena[MENU+0x2c+3-BASE]=2;v=es2_menu_code_registers(&memory,MENU,&ecx);
    CHECK(v==((uint64_t)3<<32|6)&&ecx==6);
    /* menu_toggle: entry 5 flips bit 1 of +31h and sets +36h; entry 6 and 3 leave both. */
    setup();w32(0,MENU+0x32,5);arena[MENU+0x31-BASE]=0x03;arena[MENU+0x36-BASE]=0;
    v=es2_menu_toggle_registers(&memory,MENU,entry,&ecx);
    CHECK(v==((uint64_t)0xffffffffu<<32|MENU)&&ecx==5&&b8(MENU+0x31)==0x01&&b8(MENU+0x36)==1);
    w32(0,MENU+0x32,6);arena[MENU+0x36-BASE]=0;v=es2_menu_toggle_registers(&memory,MENU,entry,&ecx);
    CHECK(v==((uint64_t)0<<32|MENU)&&ecx==0xe2&&b8(MENU+0x36)==0);
    w32(0,MENU+0x32,3);v=es2_menu_toggle_registers(&memory,MENU,entry,&ecx);
    CHECK(v==((uint64_t)0xfffffffdu<<32|MENU)&&ecx==0xe2&&b8(MENU+0x36)==0);
    /* mfd_entry: a new index is stored, the same one left. */
    setup();w32(0,GAUGE+0xb1,2);v=es2_mfd_entry_registers(&memory,GAUGE,4,entry,&ecx);
    CHECK(v==((uint64_t)4<<32|GAUGE)&&ecx==0xe2&&r32(0,GAUGE+0xb1)==4);
    /* mfd_view_next: 1 of 3 -> 2, 2 of 3 -> 0 (ECX 0). */
    setup();w32(0,GAUGE+0x318,1);w32(0,GAUGE+0x31c,3);v=es2_mfd_view_next_registers(&memory,GAUGE,entry,&ecx);
    CHECK(v==((uint64_t)2<<32|GAUGE)&&ecx==0xe2&&r32(0,GAUGE+0x318)==2);
    v=es2_mfd_view_next_registers(&memory,GAUGE,entry,&ecx);
    CHECK(v==((uint64_t)3<<32|GAUGE)&&ecx==0&&r32(0,GAUGE+0x318)==0);
}
static void test_calls(void){
    /* mfd_target: cockpit_next([49B088h]) with the entry EDX/ECX. */
    setup();const uint32_t caller[3]={0xb0,0x50,0xd0};
    uint64_t v=es2_mfd_target_registers(&memory,&graph,NULL,caller,GAUGE,entry);
    CHECK(call_count==1&&observed==1);check_call(0,0x43349c,1,COCKPIT,0,0,COCKPIT,0xe1,0xe2);CHECK(v==((uint64_t)0xd1<<32|0xa1));
    /* mfd_range: 2 + 1 = 3 wraps to 0; 447014(G, 0). */
    setup();w32(0,GAUGE+0xc5,2);v=es2_mfd_range_registers(&memory,&graph,NULL,caller,GAUGE,entry);
    check_call(0,0x447014,2,GAUGE,0,0,GAUGE,0xe1,0xe2);CHECK(v==((uint64_t)0xd1<<32|0));
    /* order_call: R+2 = the player, group order with EAX = player, EDX = group. */
    setup();w32(0,COCKPIT+0x203,MEMBER(0));w32(0,VA(0x49b0f8),GROUP);
    v=es2_order_call_registers(&memory,&graph,NULL,caller,COCKPIT,VA(0x4d03d4),entry);
    CHECK(r32(0,VA(0x4d03d4)+2)==MEMBER(0));check_call(0,0x4231a4,2,GROUP,VA(0x4d03d4),0,MEMBER(0),GROUP,0xe2);
    CHECK(v==((uint64_t)0xd1<<32|0xa1));
    /* mfd_order: the code word, order call, toggle after a nonzero EAX. */
    setup();w32(0,GAUGE+0x14,COCKPIT);v=es2_mfd_order_registers(&memory,&graph,NULL,GAUGE,entry);
    CHECK(call_count==3);check_call(0,0x43f9c0,1,MENU,0,0,0xe0,0xe1,0xe2);
    CHECK(r16(0,VA(0x4d03d4))==0xa1);
    check_call(1,0x43166c,2,COCKPIT,VA(0x4d03d4),0,COCKPIT,0xd1,MENU);
    check_call(2,0x43f9f8,1,MENU,0,0,0xa2,0xd2,0xc2);CHECK(v==((uint64_t)0xd3<<32|0xa3));
}
/* The group: count n, members MEMBER(i) with the order slot in VTABLE, the
 * byte [[B+4Dh]+9] (the member's own +100h), alive. */
static void group(unsigned n){
    w32(0,GROUP+0xc,MEMBERS);w16(0,GROUP+0x10,(uint16_t)n);
    for(unsigned i=0;i<16;++i){
        const uint32_t B=MEMBER(i);w32(0,MEMBERS+4*i,B);w32(0,B,VTABLE);
        w32(0,B+0x4d,B+0x100-8);arena[B+0x101-BASE]=0;arena[B+0x99-BASE]=0;arena[B+0x96-BASE]=0;
        distance[i]=1000u*(i+1);answer[i]=1;
    }
    w32(0,VTABLE+0x28,VTABLE+0x28);
}
static unsigned orders(unsigned *who){
    unsigned n=0;for(unsigned j=0;j<call_count;++j)if(calls[j].va==VTABLE+0x28u)who[n++]=member_of(calls[j].arg[0]);
    return n;
}
static void test_group_order(void){
    const uint32_t R=VA(0x4d03d4),f=FRAME,caller[2]={0xb0,0x50};unsigned who[16];
    /* Code 3 (rejoin), residue word 0: every member but the issuer (0) in
     * distance order (3 is nearest), each with mode 0 then 2. */
    setup();group(4);w16(0,R,3);w32(0,R+2,MEMBER(0));distance[3]=10;
    w32(0,f+8,GROUP);w32(0,f+0xc,R);w16(0,f-0x1c,0);
    uint64_t v=es2_group_order_registers(&memory,&graph,NULL,f,caller,GROUP,R,entry);
    CHECK(orders(who)==3&&who[0]==3&&who[1]==1&&who[2]==2);
    check_call(0,0x49681a,3,f-0x10,0,10,f-0x10,0xe1,0xe2);
    CHECK(calls[1].va==0x49288cu&&calls[1].arg[0]==MEMBER(1)+0x26u&&calls[1].arg[1]==MEMBER(0)+0x26u);
    /* dist_gate's entry: EAX = movsx of the byte (0) over w, EDX = the
     * issuer's position, ECX = the member's. */
    CHECK(calls[1].eax==0&&calls[1].edx==MEMBER(0)+0x26u&&calls[1].ecx==MEMBER(1)+0x26u);
    /* The order slot's third slot: the high half of Gp over the mode word. */
    for(unsigned j=0,k=0;j<call_count;++j)if(calls[j].va==VTABLE+0x28u){CHECK(calls[j].arg[2]==((GROUP&0xffff0000u)|(k?2u:0u)));++k;}
    CHECK((uint32_t)v==1&&r16(0,f-6)==2&&r16(0,f-0x1c)==0);
    CHECK(b8(f-0x10)==0&&b8(f-0xf)==1&&b8(f-0xe)==1&&b8(f-0xd)==1&&b8(f-0xc)==0);
    /* The same with the residue word -2: refused for everyone, no call
     * after memset and no dist_gate; EAX 0. */
    setup();group(4);w16(0,R,3);w32(0,R+2,MEMBER(0));w32(0,f+8,GROUP);w32(0,f+0xc,R);w16(0,f-0x1c,0xfffe);
    v=es2_group_order_registers(&memory,&graph,NULL,f,caller,GROUP,R,entry);
    CHECK(call_count==1&&(uint32_t)v==0&&r16(0,f-0x1c)==0xfffe);
    /* Code 0 (attack): w = byte [[B+4Dh]+9] = 0 -> 1; the first taker ends
     * the order (mode 0 only once); a refusal (member 1) gives mode 1. */
    setup();group(4);w16(0,R,0);w32(0,R+2,MEMBER(0));w32(0,f+8,GROUP);w32(0,f+0xc,R);answer[1]=0;
    v=es2_group_order_registers(&memory,&graph,NULL,f,caller,GROUP,R,entry);
    CHECK(orders(who)==2&&who[0]==1&&who[1]==2&&(uint32_t)v==1);
    for(unsigned j=0,k=0;j<call_count;++j)if(calls[j].va==VTABLE+0x28u){CHECK((calls[j].arg[2]&0xffffu)==k);++k;}
    CHECK(r16(0,f-0x1c)==1&&r32(0,f-4)==0);
    /* Code 4 (hold): w = (byte B+96h == 0); member 2 has it set (w 0),
     * so 1 and 3 (w 1) come first by distance, then 2. */
    setup();group(4);w16(0,R,4);w32(0,R+2,MEMBER(0));w32(0,f+8,GROUP);w32(0,f+0xc,R);arena[MEMBER(2)+0x96-BASE]=1;distance[3]=5;
    (void)es2_group_order_registers(&memory,&graph,NULL,f,caller,GROUP,R,entry);
    CHECK(orders(who)==3&&who[0]==3&&who[1]==1&&who[2]==2);
    /* Code 1 with eleven members: member 10's flag byte is the low byte of
     * the result word [f-6]; it is nearest and taken first (s = 0), its
     * mark writes 1 over s, so it is skipped in every later pass. */
    setup();group(11);w16(0,R,1);w32(0,R+2,MEMBER(0));w32(0,f+8,GROUP);w32(0,f+0xc,R);distance[10]=1;
    (void)es2_group_order_registers(&memory,&graph,NULL,f,caller,GROUP,R,entry);
    CHECK(orders(who)==10&&who[0]==10&&who[1]==1&&who[9]==9);
    /* With the frame: the memset call carries the caller's EBX/ESI, EDI = R
     * and the return address 4231c3. */
    setup();group(2);w16(0,R,3);w32(0,R+2,MEMBER(0));w32(0,f+8,GROUP);w32(0,f+0xc,R);w16(0,f-0x1c,7);
    es2_mfd_frame frame={{0,0,0,0},f-0x2c};active_frame=&frame;
    (void)es2_group_order_registers(&memory,&graph,&frame,f,caller,GROUP,R,entry);
    CHECK(calls[0].ebx==0xb0&&calls[0].esi==0x50&&calls[0].edi==R&&calls[0].ret==VA(0x4231c3));
    CHECK(calls[1].ebx==MEMBER(1)&&calls[1].esi==1&&calls[1].ret==VA(0x4232b2));
    CHECK(calls[2].va==VTABLE+0x28u&&calls[2].esi==2&&calls[2].ret==VA(0x4232fc));
}
static void test_button(void){
    /* Button found at index 8: range cycle 446ff0(G), then byte G+E5h+[G+C1h]. */
    setup();w32(0,GAUGE+0x68,12);for(unsigned i=0;i<12;++i)w32(0,GAUGE+0x18+4*i,0x5000+i);w32(0,GAUGE+0xc1,2);
    uint64_t v=es2_mfd_button_registers(&memory,&graph,NULL,0x51,0xd1,GAUGE,0x5008,entry);
    CHECK(call_count==1);check_call(0,0x446ff0,1,GAUGE,0,0,8,GAUGE+0x18+32,8);
    CHECK(b8(GAUGE+0xe5+2)==1&&v==((uint64_t)0xd1<<32|2));
    /* Index 10 with byte 49dbe0 set: nothing; not found: EAX = count. */
    setup();w32(0,GAUGE+0x68,12);for(unsigned i=0;i<12;++i)w32(0,GAUGE+0x18+4*i,0x5000+i);arena[VA(0x49dbe0)-BASE]=1;
    v=es2_mfd_button_registers(&memory,&graph,NULL,0x51,0xd1,GAUGE,0x500a,entry);
    CHECK(call_count==0&&v==((uint64_t)(GAUGE+0x18+40)<<32|10));
    v=es2_mfd_button_registers(&memory,&graph,NULL,0x51,0xd1,GAUGE,0x6000,entry);
    CHECK(call_count==0&&v==((uint64_t)(GAUGE+0x18+48)<<32|12));
    /* Index 7 in MFD mode 0: view cycle; then [[G+CDh]+0Ch] = 64h. */
    setup();w32(0,GAUGE+0x68,12);for(unsigned i=0;i<12;++i)w32(0,GAUGE+0x18+4*i,0x5000+i);w32(0,GAUGE+0xc1,0);w32(0,GAUGE+0xcd,LAMP(3));
    (void)es2_mfd_button_registers(&memory,&graph,NULL,0x51,0xd1,GAUGE,0x5007,entry);
    check_call(0,0x446fc4,1,GAUGE,0,0,7,GAUGE+0x18+28,7);CHECK(r32(0,LAMP(3)+0xc)==0x64);
    /* Button 11 (G+44h) of the radio pair 11/12 (G+44h, G+48h): byte +96h
     * of the player cleared, button 11 on (+40h set and its slot +4),
     * button 12 off and its slot +4 in any case. */
    setup();w32(0,GAUGE+0x68,13);for(unsigned i=0;i<11;++i)w32(0,GAUGE+0x18+4*i,0x5000+i);
    w32(0,COCKPIT+0x203,MEMBER(0));arena[MEMBER(0)+0x96-BASE]=5;
    w32(0,GAUGE+0x44,LAMP(0));w32(0,GAUGE+0x48,LAMP(1));arena[LAMP(0)+0x40-BASE]=0;arena[LAMP(1)+0x40-BASE]=1;
    w32(0,LAMP(0)+0x17,VTABLE);w32(0,LAMP(1)+0x17,VTABLE+8);w32(0,VTABLE+4,VTABLE+0x40);w32(0,VTABLE+0xc,VTABLE+0x44);
    (void)es2_mfd_button_registers(&memory,&graph,NULL,0x51,0xd1,GAUGE,LAMP(0),entry);
    CHECK(b8(MEMBER(0)+0x96)==0);CHECK(b8(LAMP(0)+0x40)==1);CHECK(b8(LAMP(1)+0x40)==0);CHECK(call_count==2);CHECK(slots==2);
    check_call(0,VTABLE+0x40,1,LAMP(0),0,0,VTABLE,GAUGE+0x18+44,COCKPIT);
    check_call(1,VTABLE+0x44,1,LAMP(1),0,0,LAMP(1),VTABLE+8,LAMP(0));
    /* Button 12: byte +96h set, button 11 off (its slot +4 in any case),
     * button 12 already on: no second call. */
    setup();w32(0,GAUGE+0x68,13);for(unsigned i=0;i<11;++i)w32(0,GAUGE+0x18+4*i,0x5000+i);
    w32(0,COCKPIT+0x203,MEMBER(0));arena[MEMBER(0)+0x96-BASE]=0;
    w32(0,GAUGE+0x44,LAMP(0));w32(0,GAUGE+0x48,LAMP(1));arena[LAMP(0)+0x40-BASE]=1;arena[LAMP(1)+0x40-BASE]=1;
    w32(0,LAMP(0)+0x17,VTABLE);w32(0,LAMP(1)+0x17,VTABLE+8);w32(0,VTABLE+4,VTABLE+0x40);w32(0,VTABLE+0xc,VTABLE+0x44);
    uint64_t w=es2_mfd_button_registers(&memory,&graph,NULL,0x51,0xd1,GAUGE,LAMP(1),entry);
    CHECK(b8(MEMBER(0)+0x96)==1);CHECK(b8(LAMP(0)+0x40)==0);CHECK(b8(LAMP(1)+0x40)==1);CHECK(call_count==1);
    check_call(0,VTABLE+0x40,1,LAMP(0),0,0,LAMP(0),VTABLE,COCKPIT);CHECK(w==((uint64_t)0xd1<<32|0xa1));
}
static void test_click(void){
    /* MFD mode 1, entry 2 holds the point (inclusive edges); 49dbe0 clear:
     * entry select 4470a4(G, 2), byte G+E6h. */
    setup();w32(0,GAUGE+0xc1,1);w32(0,POINT,30);w32(0,POINT+4,40);
    for(unsigned i=0;i<6;++i){w32(0,MENU+0x14+4*i,ENTRY(i));w32(0,ENTRY(i)+4,10*i+10);w32(0,ENTRY(i)+8,40);w32(0,ENTRY(i)+12,10*i+19);w32(0,ENTRY(i)+16,50);}
    w32(0,ENTRY(2)+4,30);w32(0,ENTRY(2)+12,30);
    uint64_t v=es2_mfd_click_registers(&memory,&graph,NULL,GAUGE,0x77,POINT,entry);
    CHECK(call_count==1);check_call(0,0x4470a4,2,GAUGE,2,0,ENTRY(2)+4,POINT,MENU+0x14+8);
    CHECK(b8(GAUGE+0xe6)==1&&v==((uint64_t)0xd1<<32|0xa1));
    /* The same entry, selected, with 49dbe0 set: button_press(G, 0Ah, 1), the order. */
    setup();w32(0,GAUGE+0xc1,1);w32(0,POINT,30);w32(0,POINT+4,40);
    for(unsigned i=0;i<6;++i){w32(0,MENU+0x14+4*i,ENTRY(i));w32(0,ENTRY(i)+4,10*i+10);w32(0,ENTRY(i)+8,40);w32(0,ENTRY(i)+12,10*i+19);w32(0,ENTRY(i)+16,50);}
    w32(0,ENTRY(2)+4,30);w32(0,ENTRY(2)+12,30);arena[VA(0x49dbe0)-BASE]=1;w32(0,GAUGE+0xb1,2);
    v=es2_mfd_click_registers(&memory,&graph,NULL,GAUGE,0x77,POINT,entry);
    CHECK(call_count==2);check_call(0,0x438d9c,3,GAUGE,0xa,1,ENTRY(2)+4,POINT,MENU+0x14+8);
    check_call(1,0x447248,1,GAUGE,0,0,0xa1,0xd1,0xc1);CHECK(b8(GAUGE+0xe5+1)==1&&v==((uint64_t)0xd2<<32|1));
    /* Not mode 1: the entry EAX and EDX = P. */
    setup();w32(0,GAUGE+0xc1,0);v=es2_mfd_click_registers(&memory,&graph,NULL,GAUGE,0x77,POINT,entry);
    CHECK(call_count==0&&v==((uint64_t)POINT<<32|0xe0));
}
/* pool_prev stand-in for test_setup: answers from a list, then 0. */
static const uint32_t *pool_answers;static unsigned pool_count,pool_used;
static void pool_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    if(f-IMAGE+0x400000u==0x471c70u){
        CHECK(call_count<32);call *k=calls+call_count++;memset(k,0,sizeof(*k));k->va=0x471c70;k->count=n;k->arg[0]=a[0];k->arg[1]=a[1];
        k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
        r->eax=pool_used<pool_count?pool_answers[pool_used++]:0;r->edx=0x1111;r->ecx=0x22220000u;return;
    }
    fake_call(c,f,a,n,r);
}
static void test_setup(void){
    /* Actors backwards: MEMBER(12) (byte A3h clear), MEMBER(13) (set; its
     * group: the player MEMBER(9), then MEMBER(1..4)): wingmen 1..3, n = 3,
     * so the walk stops without a third pool_prev call. */
    setup();w32(0,COCKPIT+0x203,MEMBER(9));w32(0,COCKPIT+0x1f9,0x1f9);w32(0,COCKPIT+0x1ed,0x1ed);w32(0,VA(0x4a9b7a),0x7a);
    w32(0,GROUP+0xc,MEMBERS);w16(0,GROUP+0x10,5);
    w32(0,MEMBERS,MEMBER(9));for(unsigned i=1;i<5;++i){w32(0,MEMBERS+4*i,MEMBER(i));w16(0,MEMBER(i)+0x29c,(uint16_t)(0x100+i));}
    arena[MEMBER(12)+0xa3-BASE]=0;arena[MEMBER(13)+0xa3-BASE]=1;w32(0,MEMBER(13)+0x45,GROUP);
    for(unsigned k=0;k<3;++k)w32(0,VA(0x4d03c8)+4*k,0);
    static const uint32_t prevs[2]={MEMBER(12),MEMBER(13)};
    pool_answers=prevs;pool_count=2;pool_used=0;
    const es2_weapon_graph g={IMAGE,0,0,0,0,pool_call,0,fake_observe,0};
    const uint64_t v=es2_order_setup_registers(&memory,&g,COCKPIT,entry);
    CHECK(r32(0,VA(0x49b0f8))==GROUP);
    CHECK(r32(0,VA(0x4d03c8))==MEMBER(1)&&r32(0,VA(0x4d03c8)+4)==MEMBER(2)&&r32(0,VA(0x4d03c8)+8)==MEMBER(3));
    CHECK(call_count==2+3+1);
    /* pool_prev(list, prev) with EAX = ECX = prev, EDX = the list. */
    check_call(0,0x471c70,2,0x7a,0,0,0,0x7a,0);
    check_call(1,0x471c70,2,0x7a,MEMBER(12),0,MEMBER(12),0x7a,MEMBER(12));
    /* The HDD panels (HDD, i, ECX = the wingman's word +29Ch over ECX's
     * high half): first over the group pointer's (431575), then over the
     * previous call's ECX (C3h, C4h: high half 0). */
    check_call(2,0x44a7e8,3,0x1f9,0,(GROUP&0xffff0000u)|0x101,0x1f9,MEMBER(1),(GROUP&0xffff0000u)|0x101);
    check_call(3,0x44a7e8,3,0x1f9,1,0x102,0x1f9,MEMBER(2),0x102);
    check_call(4,0x44a7e8,3,0x1f9,2,0x103,0x1f9,MEMBER(3),0x103);
    /* The MFD list (MFD, n) with the last panel call's EAX/ECX. */
    check_call(5,0x4472bc,2,0x1ed,3,0,0xa5,0x1ed,0xc5);
    CHECK(v==((uint64_t)0xd6<<32|0xa6));
}
int main(void){
    test_leaves();test_calls();test_group_order();test_button();test_click();test_setup();
    printf("mfd_orders: %u checks passed\n",checks);
    return 0;
}
