#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/mission_root.h"
/* Synthetic state for the core (the mission-load root 461608). The
 * arena covers the image globals 490000..6cc000 (IMAGE is placed so that
 * VA(490000h) = BASE), then the objects and the frame. A scripted call
 * function stands in for every game call: it logs the callee (original
 * VA), the argument slots, the entry EAX/EDX/ECX and EBX/ESI/EDI and return
 * address the core names, answers pool_prev from a per-pool list, new from
 * a list of blocks, dist_gate from a list of distances, and fills the two
 * rectangles and the launch peek's words. Expected values are worked out by
 * hand from the contract in mission_load.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x90000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJ(k) (BASE+0x240000u+0x400u*(uint32_t)(k))
#define F (BASE+0x258000u)                    /* the frame's EBP */
static uint8_t arena[0x260000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mission_root line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void mw16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void mw32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w16(uint32_t a,uint32_t v){mw16(0,a,(uint16_t)v);}
static void w32(uint32_t a,uint32_t v){mw32(0,a,v);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,mw32,mw16,host};
#define G32(a) r32(0,(a))
#define G16(a) r16(0,(a))
#define G8(a) (*(const uint8_t *)host(0,(a)))
typedef struct call { uint32_t va,n,a[6],eax,edx,ecx,ebx,esi,edi,ret; } call;
static call calls[256];static unsigned call_count,observed,observed_slot;
static es2_mission_root frame;
/* Scripts: pool_prev answers per pool (0 ends), new's blocks, distances. */
static uint32_t prev_list[4][8];static unsigned prev_at[4];
static uint32_t new_list[8];static unsigned new_at;
static uint32_t dist_list[4];static unsigned dist_at;
static unsigned pool_index(uint32_t pool){
    if(pool==OBJ(40))return 0;   /* explosions */
    if(pool==OBJ(41))return 1;   /* debris */
    if(pool==OBJ(42))return 2;   /* MECH */
    return 3;                    /* buildings */
}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<256);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;k->n=n;
    for(unsigned i=0;i<n&&i<6;++i)k->a[i]=a[i];
    k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;k->ebx=frame.call.ebx;k->esi=frame.call.esi;k->edi=frame.call.edi;
    k->ret=frame.call.ret-IMAGE+0x400000u;
    r->eax=0xa0000u+call_count;r->edx=0xd0000u+call_count;r->ecx=0xc0000u+call_count;
    switch(k->va){
    case 0x49694c:w32(a[1],10);w32(a[1]+4,20);w32(a[1]+8,650);w32(a[1]+12,500);break;   /* window rect */
    case 0x496970:w32(a[1],0);w32(a[1]+4,0);w32(a[1]+8,640);w32(a[1]+12,480);break;    /* client rect */
    case 0x4614c0:w16(a[0],2);w16(a[1],1);break;                                          /* world 2, flyer */
    case 0x47749c:r->eax=new_list[new_at++];break;
    case 0x471c70:{const unsigned p=pool_index(a[0]);r->eax=prev_list[p][prev_at[p]++];break;}
    case 0x49288c:r->eax=dist_list[dist_at++];break;
    case 0x46e180:r->eax=1000;break;                                                            /* ground height */
    case 0x437c8c:r->eax=0x5550000u+a[0];break;
    default:break;
    }
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;++observed;if(slot==0x4a5824u)++observed_slot;}
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
#define B VA(0x4d24bcu)
static uint64_t run(uint32_t ebx,uint32_t *ecx){
    es2_weapon_graph g={IMAGE,F-ES2_MISSION_ROOT_LOCALS,0,0,0,fake_call,0,fake_observe,0};
    memset(&frame,0,sizeof(frame));
    return es2_mission_root_registers(&memory,&g,&frame,ebx,entry,ecx);
}
/* The index of the k-th call of `va` (from 0), or -1. */
static int find(uint32_t va,unsigned k){for(unsigned i=0;i<call_count;++i)if(calls[i].va==va&&k--==0)return (int)i;return -1;}
static unsigned count(uint32_t va){unsigned n=0;for(unsigned i=0;i<call_count;++i)n+=calls[i].va==va;return n;}
static void setup(void){
    memset(arena,0,sizeof(arena));call_count=observed=observed_slot=0;
    memset(prev_at,0,sizeof(prev_at));new_at=dist_at=0;memset(new_list,0,sizeof(new_list));memset(prev_list,0,sizeof(prev_list));
    w32(VA(0x4d3610u),0x77);                      /* hwnd */
    w32(VA(0x49f89au),0x005f646cu);               /* "ld_\0" */
    w32(VA(0x6cbee4u),OBJ(60));w32(OBJ(60),100);/* construction counter */
    w32(VA(0x4a961eu),OBJ(40));w32(VA(0x4a9642u),OBJ(41));w32(VA(0x4a9b7au),OBJ(42));w32(VA(0x4a95a0u),OBJ(43));
    w32(VA(0x4a9622u),OBJ(44));w32(VA(0x4a9646u),OBJ(45));
    w32(VA(0x4a0be0u),OBJ(46));                   /* terrain object */
    w32(VA(0x6c5eb8u),OBJ(47));                   /* key device */
    w32(VA(0x4a5824u),0x12345678u);               /* graphics slot target */
}
/* Scenario 1: every optional branch taken. */
static void test_full(void){
    setup();
    w8(VA(0x4d267du),1);w8(B+0xc,1);w8(B+0xb5,1);w32(B+0x82,600);w8(B+0x7b,1);w8(B+0xb,0);
    w32(VA(0x4d3040u),1201);w8(B+0x7c,0);w32(VA(0x49f8ecu),1);w16(VA(0x4aa728u),0);w32(B+0xb7,-7);
    w16(VA(0x49abbau),0x4321);w8(VA(0x4d1f41u),3);w8(VA(0x4d1f42u),2);w8(VA(0x4d1f40u),1);
    w32(VA(0x49b088u),OBJ(50));w32(B+0x12,OBJ(51));
    /* terrain objects: count 3, entries T1, T2, 0 (the null ends it) */
    w16(VA(0x4aaa7cu),3);w32(VA(0x4aaa78u),OBJ(52));w32(OBJ(52),OBJ(53));w32(OBJ(52)+4,OBJ(54));w32(OBJ(52)+8,0);
    /* explosions E1; debris none; MECHs M1, M2 (M2 the player); buildings B1 (category 1), B2, B3 */
    prev_list[0][0]=OBJ(1);prev_list[2][0]=OBJ(2);prev_list[2][1]=OBJ(3);
    prev_list[2][3]=OBJ(2);prev_list[2][4]=OBJ(3);                       /* the display-object walk (after the 0 at [2]) */
    prev_list[3][0]=OBJ(4);prev_list[3][1]=OBJ(5);prev_list[3][2]=OBJ(6);
    for(unsigned k=2;k<=3;++k){w32(OBJ(k)+0x26,100*k);w32(OBJ(k)+0x2a,200*k);w32(OBJ(k)+0x2e,300*k);w32(OBJ(k)+0x1f2,OBJ(20+k));}
    w16(OBJ(22)+0x16,(uint16_t)-50);w16(OBJ(22)+0x50,0);w16(OBJ(23)+0x16,70);w16(OBJ(23)+0x50,1);
    w8(OBJ(3)+0x92+0x11,1);
    w32(OBJ(3)+0x45,OBJ(30));w16(OBJ(30)+0x10,2);                 /* the player's group: two members */
    w32(OBJ(4)+0x45,OBJ(31));w8(OBJ(31)+0x12,1);w32(OBJ(5)+0x45,OBJ(32));w8(OBJ(32)+0x12,0);w32(OBJ(6)+0x45,OBJ(32));
    dist_list[0]=50000;dist_list[1]=39999;
    new_list[0]=OBJ(10);new_list[1]=OBJ(11);new_list[2]=OBJ(12);new_list[3]=OBJ(13);new_list[4]=OBJ(14);
    uint32_t ecx;w32(F-0x30,0x3a3a3a3au);
    const uint64_t v=run(0xb0b0b0b0u,&ecx);
    /* the call sequence */
    static const uint32_t order[]={0x49630c,0x479cfc,0x45a5dc,0x4955c4,0x49694c,0x496970,0x461294,0x464c78,0x46548c,0x4614c0,
        0x4967ea,0x492bec,0x465f7c,0x481fe0,0x464c78,0x461450,0x47749c,0x480af0,0x462410,0x463370,0x48dd08,0x4913dc,0x401d94,
        0x46134c,0x467840,0x424308,0x4282d8,0x4282d8,0x452ae4,0x4253d8,0x44f6f4,0x464c78,
        0x471c70,0x478628,0x407b3c,0x4077e8,0x409904,0x471c70,0x471c70,
        0x471c70,0x46e180,0x471c70,0x46e180,0x471c70,
        0x47749c,0x48bd84,0x47749c,0x48bd84,0x464c78,0x46548c,0x464c78,0x492bec,0x45cdf4,0x465618,0x4544c0,
        0x47749c,0x48bda4,0x45edb0,0x47749c,0x429660,0x431530,0x471c70,0x437c8c,0x471c70,0x437c8c,0x471c70,
        0x41b130,0x4011a0,0x42a1f0,0x42a1f0,0x471c70,0x471c70,0x49288c,0x471c70,0x49288c,
        0x46313c,0x42eb40,0x43048c,0x45fca8,0x464c78,0x43048c,0x464c78,0x43122c,0x4328cc,0x433f7c,0x12345678u,
        0x45d490,0x45d50c,0x45b758,0x464c78,0x477abc};
    CHECK(call_count==sizeof(order)/sizeof(order[0]));
    for(unsigned i=0;i<call_count;++i)CHECK(calls[i].va==order[i]);
    CHECK(observed==call_count&&observed_slot==1);
    /* registers at the first call: EAX the descriptor, EBX the caller's, ESI f-8, EDI the view block */
    CHECK(calls[0].eax==VA(0x49f7c9u)&&calls[0].edx==0xe1u&&calls[0].ecx==0xe2u);
    CHECK(calls[0].ebx==0xb0b0b0b0u&&calls[0].esi==F-8u&&calls[0].edi==B&&calls[0].ret==0x461626u);
    CHECK(calls[1].a[0]==VA(0x461304u)&&calls[1].n==1);
    /* the rectangles: (hwnd, f-108h), then (hwnd, f-118h) with ECX = f-118h */
    {const call *k=calls+find(0x49694c,0);CHECK(k->a[0]==0x77&&k->a[1]==F-0x108u&&k->ecx==0x77&&k->edx==F-0x108u);}
    {const call *k=calls+find(0x496970,0);CHECK(k->a[0]==0x77&&k->a[1]==F-0x118u&&k->eax==0x77&&k->ecx==F-0x118u&&k->edx==480);}
    CHECK(G32(F-0x34)==640&&G32(F-0x38)==480&&G32(F-0x3c)==0&&G32(F-0x40)==0);
    /* the loading screen name: "flyr" for the flag, character 7 = '3' */
    {const call *k=calls+find(0x4614c0,0);CHECK(k->a[0]==F-0x44u&&k->a[1]==F-0x42u&&k->eax==F-0x42u&&k->edx==F-0x44u);}
    {const call *k=calls+find(0x4967ea,0);CHECK(k->a[0]==F-0x128u&&k->a[1]==VA(0x49f89eu)&&k->eax==F-0x128u);}
    CHECK(G32(F-0x128)==0x005f646cu&&G8(F-0x121)=='3');
    {const call *k=calls+find(0x492bec,0);CHECK(k->a[0]==F-0x128u&&k->edx==F-0x128u&&(k->ecx&0xff)=='3');}
    /* the palette: its result into f-48h and ECX for 481fe0 */
    {const int i=find(0x465f7c,0);CHECK(G32(F-0x48)==0xa0000u+(unsigned)i+1u);
     CHECK(calls[i+1].ecx==G32(F-0x48)&&calls[i+1].a[0]==G32(F-0x48));}
    /* new(6) -> O10, 480af0(O10, 26D3h), the counter decremented, view+4Eh */
    {const call *k=calls+find(0x480af0,0);CHECK(k->a[0]==OBJ(10)&&k->a[1]==0x26d3u&&k->edx==OBJ(10));}
    CHECK(G32(B+0x4e)==OBJ(10));
    /* sound: slot 0 the sound byte, the five residue words (zero here) with the register high words */
    {const call *k=calls+find(0x462410,0);CHECK(k->n==6&&k->a[0]==1&&k->edx==1);
     CHECK(k->a[1]==(OBJ(10)&0xffff0000u)&&k->a[4]==(OBJ(10)&0xffff0000u)&&k->a[3]==0);}
    CHECK(G32(F-0x4c)==1);
    CHECK(calls[find(0x401d94,0)].a[0]==2);
    CHECK(G32(B+0xbb)==25);
    /* the binning: T1, T2, then the null entry; count word 3 -> 0 after three decrements */
    CHECK(calls[find(0x4282d8,0)].a[0]==OBJ(53)&&calls[find(0x4282d8,1)].a[0]==OBJ(54));
    CHECK(G16(F-0x68)==0&&G32(F-0x6c)==OBJ(52)+12);
    /* the drain: removal_append(list, E1) with EAX = EBX = E1 */
    {const call *k=calls+find(0x478628,0);CHECK(k->a[0]==OBJ(44)&&k->a[1]==OBJ(1)&&k->eax==OBJ(1)&&k->ebx==OBJ(1));}
    {const call *k=calls+find(0x407b3c,0);CHECK(k->ebx==OBJ(1)&&k->ret==0x461853u);}
    CHECK(calls[find(0x471c70,1)].a[0]==OBJ(40)&&calls[find(0x471c70,2)].a[0]==OBJ(41)&&calls[find(0x471c70,2)].ret==0x46189bu);
    CHECK(G16(VA(0x4cf880u))==0x4321);
    /* the height fix: z = 1000 + type offset (+5000), x/y kept; the player M2 */
    {const call *k=calls+find(0x46e180,0);CHECK(k->a[0]==OBJ(46)&&k->a[1]==F-0x134u&&k->ebx==OBJ(2));}
    CHECK(G32(OBJ(2)+0x2e)==950&&G32(OBJ(2)+0x26)==200&&G32(OBJ(2)+0x2a)==400);
    CHECK(G32(OBJ(3)+0x2e)==1000+70+5000&&G32(F-0x12c)==6070);
    CHECK(G32(B+0x2a)==OBJ(3)&&G32(VA(0x4d2684u))==OBJ(3)&&G32(OBJ(46)+0x125)==VA(0x428c60u));
    CHECK(calls[find(0x471c70,3)].a[1]==0&&calls[find(0x471c70,4)].a[1]==OBJ(2)&&G32(F-0x74)==OBJ(3));
    /* the group cameras: two, EBX = 4D2688h + 4k, ESI = f-8 */
    {const call *k=calls+find(0x48bd84,1);CHECK(k->a[0]==OBJ(12)&&k->ebx==VA(0x4d2688u)+4u&&k->esi==F-8u);}
    CHECK(G32(VA(0x4d2688u))==OBJ(11)&&G32(VA(0x4d268cu))==OBJ(12)&&G32(F-8)==OBJ(12));
    CHECK(G32(OBJ(12))==VA(0x49f8e8u)&&G32(F-0x94)==OBJ(12)+0x44u&&G32(F-0x80)==2&&G16(F-0x7a)==2);
    CHECK(G32(OBJ(60))==100-1+1-3+1-3+1-3);
    CHECK(G32(B+0x60)==OBJ(3));
    /* the cursor (width 600 < 640) */
    {const int i=find(0x492bec,1);CHECK(calls[i].a[0]==VA(0x49f8aau)&&calls[i+1].a[0]==G32(F-0x98)&&calls[i+2].a[0]==G32(F-0x9c));}
    /* the dead computation: mode byte 1 -> f-0A0h = 1 (byte +0Bh is 0), f-0A4h = 0 */
    CHECK(G32(F-0xa0)==1&&G32(F-0xa4)==0);
    CHECK(G32(VA(0x49dd94u))==VA(0x461250u)&&G32(VA(0x49dd98u))==VA(0x461274u));
    /* the main camera: 9 (above 1200), the vectors */
    {const call *k=calls+find(0x48bda4,0);
     CHECK(k->n==6&&k->a[0]==OBJ(13)&&k->a[1]==OBJ(51)&&k->a[2]==9&&k->a[3]==0x100&&k->a[4]==F-0x140u&&k->a[5]==F-0xb8u);
     CHECK(k->eax==OBJ(13)&&k->ecx==OBJ(51)&&k->edx==9);}
    CHECK(G32(F-0x13c)==0xffffe890u&&G32(B+0x2e)==OBJ(13)&&G16(OBJ(13)+0x36)==2);
    CHECK(G16(B+0x6e)==0);
    /* the cockpit at view+4 */
    {const call *k=calls+find(0x429660,0);CHECK(k->a[0]==OBJ(14)&&k->a[1]==OBJ(3));}
    CHECK(G32(B+4)==OBJ(14));
    /* the display objects: ESI the previous iterator */
    {const call *k=calls+find(0x471c70,7);CHECK(k->esi==OBJ(2)&&k->a[1]==OBJ(2));}
    CHECK(G32(OBJ(2)+0x1fe)==0x5550000u+OBJ(2)&&G32(OBJ(3)+0x1fe)==0x5550000u+OBJ(3));
    {const call *k=calls+find(0x41b130,0);CHECK(k->a[0]==OBJ(3)&&k->ebx==0&&k->esi==OBJ(3));}
    CHECK(calls[find(0x42a1f0,0)].a[1]==1&&calls[find(0x42a1f0,1)].a[1]==0);
    /* the building search: B1 skipped (category 1), B2 at 50000, B3 at 39999 ends it */
    {const call *k=calls+find(0x49288c,0);CHECK(k->a[0]==OBJ(5)+0x26u&&k->a[1]==OBJ(3)+0x26u&&k->ebx==OBJ(5)+0x26u);}
    CHECK(count(0x49288c)==2&&G32(F-0xe0)==OBJ(6));
    /* the CD track: (-7 mod 5) + 2 = 0 (idiv rounds toward zero) */
    CHECK(G32(VA(0x49f8fcu))==0&&G8(VA(0x49f900u))==1&&calls[find(0x46313c,0)].edx==0&&calls[find(0x46313c,0)].ecx==5);
    CHECK(G16(VA(0x49aef6u))==8&&calls[find(0x42eb40,0)].a[0]==OBJ(13));
    /* the text rectangle (0, 0, 320, 400) */
    {const call *k=calls+find(0x43122c,0);CHECK(k->a[0]==1&&k->a[1]==F-0x150u&&k->a[2]==0&&k->eax==F-0x150u);}
    CHECK(G32(F-0x150)==0&&G32(F-0x14c)==0&&G32(F-0x148)==320&&G32(F-0x144)==400);
    CHECK(G8(VA(0x49b105u))==1);
    /* the detail levels */
    CHECK(G16(B+0x16)==3&&G16(B+0x18)==3&&G16(B+0xb1)==2&&G16(B+0xb3)==2&&G16(B+0x3a)==1);
    CHECK((calls[find(0x45d490,0)].a[0]&0xffff)==3&&(calls[find(0x45d50c,0)].a[0]&0xffff)==2);
    CHECK(G8(VA(0x4d1dd6u))==1);
    {const call *k=calls+find(0x477abc,0);CHECK(k->a[0]==OBJ(47)&&k->edx==OBJ(47)&&k->ret==0x461f60u);}
    CHECK(G32(F-0xf8)==OBJ(47));
    /* the result: 477abc's EDX:EAX, ECX the record's head */
    CHECK((uint32_t)v==0xa0000u+call_count&&(uint32_t)(v>>32)==0xd0000u+call_count&&ecx==0x3a3a3a3au);
}
/* Scenario 2: every optional branch skipped, every new null. */
static void test_empty(void){
    setup();
    w8(B+0xb5,0);w32(B+0x82,640);w8(B+0x7b,3);w16(B+0xaa,1);w8(B+0xb,0);w32(VA(0x4d3040u),1200);w8(B+0x7c,2);
    w32(VA(0x49f8ecu),0);w16(B+0x54,1);w16(B+0x1a,1);w8(VA(0x4d1dd6u),7);
    w16(VA(0x4aaa7cu),0);w32(VA(0x4aaa78u),OBJ(52));
    /* one MECH, the player (the group read needs one); its group count -1 builds no camera */
    prev_list[2][0]=OBJ(2);w32(OBJ(2)+0x1f2,OBJ(22));w8(OBJ(2)+0x92+0x11,1);w32(OBJ(2)+0x45,OBJ(30));w16(OBJ(30)+0x10,(uint16_t)-1);
    prev_list[2][2]=0;
    new_list[1]=OBJ(13);                /* the main camera (the original reads it at 461c6e) */
    uint32_t ecx;w32(F-0x30,0x11);
    run(0,&ecx);
    static const uint32_t order[]={0x49630c,0x479cfc,0x45a5dc,0x49694c,0x496970,0x461294,0x464c78,0x46548c,0x4614c0,
        0x4967ea,0x492bec,0x465f7c,0x481fe0,0x464c78,0x461450,0x47749c,0x462410,0x463370,0x48dd08,0x4913dc,0x401d94,
        0x46134c,0x467840,0x424308,0x452ae4,0x4253d8,0x44f6f4,0x464c78,0x471c70,0x471c70,0x471c70,0x46e180,0x471c70,
        0x464c78,0x46548c,0x464c78,0x4544c0,0x47749c,0x48bda4,0x45edb0,0x47749c,0x431530,0x471c70,
        0x41b130,0x4011a0,0x42a1f0,0x471c70,0x45d548,0x464c78,0x43048c,0x464c78,0x464a14,0x4328cc,0x433f7c,0x12345678u,
        0x45d490,0x45d50c,0x45d85c,0x45b758,0x464c78,0x477abc};
    CHECK(call_count==sizeof(order)/sizeof(order[0]));
    for(unsigned i=0;i<call_count;++i)CHECK(calls[i].va==order[i]);
    CHECK(G32(B+0x4e)==0&&G32(B+0x2e)==OBJ(13)&&G32(B+4)==0&&G32(B+0xbb)==13);
    CHECK(G16(F-0x68)==0xffff);
    CHECK(G32(F-0xa0)==0&&G32(F-0xb0)==8);   /* mode 3, present word set */
    CHECK(G8(VA(0x4d1dd6u))==0&&ecx==0x11);  /* the input byte stays 0 under tape playback */
    CHECK(calls[find(0x42a1f0,0)].a[1]==0);
}
int main(void){
    test_full();test_empty();
    printf("PASS: mission_root %u checks\n",checks);
    return 0;
}
