#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/vehicle_update.h"
/* Synthetic checks of the core on an own arena with scripted callees:
 * the two device readers, the condition record, the weapon tally, the
 * combat rating, the tone state machine and two paths of the per-vehicle
 * update (no target; a visible target with lock roll and request flags).
 * Values are hand-derived; the in-vitro harness compares against the
 * original. */
#define BASE UINT32_C(0x90000000)
#define IMAGE UINT32_C(0x00400000)
static uint8_t arena[16384];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"vehicle_update line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;if(!(a-BASE<sizeof(arena)||a-IMAGE<0x2d8000u))fprintf(stderr,"address %08x\n",(unsigned)a);CHECK(a-BASE<sizeof(arena)||a-IMAGE<0x2d8000u);
    static uint8_t image_bytes[0x2d8000];return a-BASE<sizeof(arena)?arena+(a-BASE):image_bytes+(a-IMAGE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint8_t scratch_bytes[128];
/* Virtual slots are fake addresses 0x7000+slot in every table. */
#define VT (BASE+0x3f00u)
static unsigned calls;static uint32_t functions[64],args[64][3];static es2_weapon_registers entries[64];
static uint32_t fire_owner;
static uint16_t timer_result,clock_result,rng_result,v60_index;static uint32_t dist_result,sight_result;
static uint16_t record_word(unsigned i){return (uint16_t)(i==0?0x80:i==20||i==33?0xffff:i==21||i==32?0xc8:0x100);}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);functions[calls]=f;entries[calls]=*r;for(unsigned i=0;i<n&&i<3;++i)args[calls][i]=a[i];++calls;
    switch(f){
    case 0x4151a4:CHECK(n==2);for(unsigned i=0;i<41;++i)w16(0,a[1]+2+2*i,record_word(i));
        r->eax=0x1111;r->edx=0x2222;break;
    case 0x41b4ec:CHECK(n==2);for(unsigned i=0;i<41;++i)w16(0,a[1]+2+2*i,record_word(i));break;
    case 0x47e0a0:CHECK(n==2);r->eax=(uint32_t)(((int32_t)a[0]*(int32_t)a[1])>>8);r->edx=0x4444;break;
    case 0x49681a:CHECK(n==3&&a[1]==0);memset(host(0,a[0]),0,a[2]);r->eax=a[0];r->edx=a[2];r->ecx=0;break;
    case 0x7054:CHECK(n==1);r->eax=0xffff0001u;break;                          /* V54 */
    case 0x7060:CHECK(n==2);if(a[1])w16(0,a[1],7);r->eax=a[0]==BASE+0x2100u?v60_index:5u;break; /* V60 */
    case 0x7000:CHECK(n==3);r->eax=0x00000020u;break;                          /* V0 */
    case 0x4107e4:CHECK(n==4);fire_owner=a[3];r->eax=0x5a5a0003u;r->edx=0x6b6b0000u;r->ecx=0x7c7c7c7cu;break; /* group_fire */
    case 0x467924:CHECK(n==1);r->eax=0x10;r->edx=0xabcd0000u;break;
    case 0x413b38:CHECK(n==2&&a[1]==0x20);r->eax=0x99990064u;break;
    case 0x467aa8:r->eax=1;break;
    case 0x467a48:r->eax=timer_result;break;
    case 0x467828:r->eax=clock_result;break;
    case 0x462830:r->eax=0x5150;break;
    case 0x492ee0:CHECK(a[0]==IMAGE+0xd2599u);r->eax=rng_result;break;
    case 0x49288c:r->eax=dist_result;break;
    case 0x492934:r->eax=0x1000;break;
    case 0x412608:r->eax=sight_result;break;
    case 0x41edd8:case 0x41aa44:case 0x41aa10:case 0x40fbdc:case 0x41b0bc:
        if(f==0x40fbdc){w16(0,a[1],1);w16(0,a[1]+2,0);w16(0,a[1]+4,1);w16(0,a[1]+6,0);w16(0,a[1]+8,1);}
        if(f==0x41aa44)r->eax=1;
        if(f==0x41aa10)r->eax=0x10000u;
        break;
    default:fprintf(stderr,"unexpected callee %08x\n",(unsigned)f);CHECK(0);
    }
}
static es2_weapon_graph graph(void){
    es2_weapon_graph g={IMAGE,(uint32_t)(BASE+0x3800u),0,0,0,call,NULL,NULL,NULL};(void)scratch_bytes;return g;
}
static unsigned find(uint32_t f){for(unsigned i=0;i<calls;++i)if(functions[i]==f)return i;CHECK(0);return 0;}
static unsigned count_calls(uint32_t f){unsigned k=0;for(unsigned i=0;i<calls;++i)k+=functions[i]==f;return k;}
int main(void){
    const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
    for(unsigned s=0;s<0x70;s+=4)w32(0,VT+s,0x7000u+s);
    es2_weapon_graph g=graph();
    const uint32_t A=BASE+0x400,D=BASE+0x1000,C=BASE+0x1100,G=BASE+0x2000,T=BASE+0x800;
    /* device_state / device_word. */
    CHECK(es2_device_state_registers(&memory,A)==0);
    CHECK(es2_device_word_registers(&memory,A)==0);
    w32(0,A+0x307,D);w16(0,D+0x7d,0x1234);w16(0,D+0x7f,0x8001);w8(A+0x96,0xfe);
    CHECK(es2_device_state_registers(&memory,A)==((uint64_t)0xfffffffeu<<32|0xfffffffeu));
    w8(A+0xa3,1);
    CHECK(es2_device_state_registers(&memory,A)==((uint64_t)(((A+0x92)&0xffff0000u)|0x1234)<<32|(((A+0x92)&0xffff0000u)|0x1234)));
    CHECK(es2_device_word_registers(&memory,A)==((uint64_t)D<<32|0x8001));
    /* condition_record. */
    w32(0,A+0x1f2,C);w16(0,C+0x6c,0x77);calls=0;
    uint32_t out=BASE+0x1800;
    CHECK(es2_condition_record_registers(&memory,&g,A,out,entry)==((uint64_t)0x2222<<32|0x1111));
    CHECK(r16(0,out)==0x77&&calls==1&&functions[0]==0x4151a4&&args[0][0]==A+0x206&&args[0][1]==out);
    CHECK(entries[0].eax==A+0x206&&entries[0].edx==out&&entries[0].ecx==((C&0xffff0000u)|0x77));
    /* weapon_tally: three slots (null, weapon 1 -> index v60_index, weapon 2 -> 5). */
    w32(0,G,VT);w32(0,G+4,G+0x80);w16(0,G+8,3);
    w32(0,G+0x80,0);w32(0,G+0x84,BASE+0x2100);w32(0,G+0x88,BASE+0x2200);
    w32(0,BASE+0x2100,VT);w32(0,BASE+0x2200,VT);
    for(unsigned i=0;i<5;++i)w16(0,out+2*i,0x5555);
    v60_index=1;calls=0;
    uint64_t res=es2_weapon_tally_registers(&memory,&g,G,out,entry);
    CHECK((uint32_t)res==0&&(uint16_t)(res>>32)==0);
    CHECK(r16(0,out)==0&&r16(0,out+2)==7&&r16(0,out+4)==0&&calls==3&&count_calls(0x7060)==2);
    CHECK(entries[0].ecx==((entry[2]&0xffff0000u)|3)&&entries[0].eax==G+4&&entries[0].edx==G+0x80);
    /* combat_rating: base word 0x300, weapons 1 (V54 = 1, value 0x40) of 3,
     * groups all 0x100 except 0 (0x80), 20/21 handled in loop 1. */
    w16(0,C+2+0x42,0x300);w32(0,A+0x202,G);w32(0,A+0x212,BASE+0x2500);w32(0,BASE+0x2500,BASE+0x2400);
    for(unsigned i=0;i<19;++i)w16(0,BASE+0x2400+18*i,(uint16_t)(i==0?0x200:0x10));
    w32(0,BASE+0x2100+0x1c,BASE+0x2300);w16(0,BASE+0x2300+0x4e,0x40);w32(0,BASE+0x2200+0x1c,BASE+0x2300);
    w16(0,C+2+0x7c,3);
    calls=0;res=es2_combat_rating_registers(&memory,&g,A,entry);
    /* Loop 1: slot 0 null; slot 1 c = word 32 = 0C8h -> 40h*(100h-0C8h)>>8 = 14;
     * slot 2 c = word 33 negative, skipped before V54. Loop 2: group 0 c = 80h
     * -> 200h*80h>>8 = 256, the others 100h -> 0. Loop 3: parts 0 and 2 are
     * above 0B4h and subtract P+7Ch (3) and P+80h (0). */
    CHECK(r16(0,A+0x29e)==(uint16_t)((0x300+14+256-3)>>4)&&r8(A+0x94)==1);
    CHECK((uint32_t)res==(((A+0x92)&0xffff0000u)|(uint16_t)((0x300+14+256-3)>>4)));
    CHECK(count_calls(0x41b4ec)==1&&count_calls(0x7054)==1&&count_calls(0x47e0a0)==1+19);
    /* ready_tone. */
    const uint32_t t0=IMAGE+0x9a1d0u,t1=IMAGE+0x9a1d1u;
    w8(A+0x9b,1);clock_result=0x40;calls=0;
    CHECK((uint32_t)es2_ready_tone_registers(&memory,&g,A,entry)==0x5150&&r8(t0)==1&&r8(t1)==1&&args[1][0]==0x15);
    calls=0;es2_ready_tone_registers(&memory,&g,A,entry);CHECK(calls==1);
    clock_result=0;es2_ready_tone_registers(&memory,&g,A,entry);CHECK(r8(t0)==0);
    w8(A+0x9b,0);calls=0;es2_ready_tone_registers(&memory,&g,A,entry);CHECK(calls==1&&args[0][0]==0x16&&r8(t1)==0);
    calls=0;CHECK(es2_ready_tone_registers(&memory,&g,A,entry)==((uint64_t)(A+0x92)<<32|A)&&calls==0);
    w8(A+0x9d,1);calls=0;es2_ready_tone_registers(&memory,&g,A,entry);CHECK(calls==1&&args[0][0]==0x14);w8(A+0x9d,0);
    /* vehicle_update without a target. */
    w16(0,A+0x256,5);w16(0,A+0x292,100);w16(0,A+0x222,1000);w16(0,A+0x224,600);w8(A+0xb0,9);w8(A+0x94,1);
    timer_result=0;calls=0;
    res=es2_vehicle_update_registers(&memory,&g,A,entry);
    CHECK(r16(0,A+0x292)==0x64+500&&r8(A+0xb0)==0&&r16(0,A+0x286)==0xfa0&&r8(A+0xa1)==0);
    CHECK(entries[0].eax==((entry[0]&0xffff0000u)|5)&&args[0][0]==entries[0].eax);
    CHECK(functions[1]==0x7000&&args[1][1]==((0xabcd0000u)|(uint16_t)(0x74-500))&&args[1][2]==A);
    CHECK(count_calls(0x41edd8)==0&&count_calls(0x41aa44)==0&&count_calls(0x40fbdc)==1&&count_calls(0x41b0bc)==0);
    for(unsigned i=0;i<5;++i)CHECK(r16(0,G+0xa+2*i)==0);
    CHECK(res==((uint64_t)5<<32|0));
    /* vehicle_update with a visible target straight ahead in range. */
    w32(0,A+0x1a4,T);w16(0,T+0x1a8,0);w8(T+0xa1,1);w16(0,C+0x50,0);w16(0,A+0x10,0x1000);w16(0,A+0x298,0);
    w32(0,A+0x45,BASE+0x2600);w8(BASE+0x2600+0x12,0);w8(A+0x96,1);w8(A+0x9c,0);w8(A+0x9d,0);
    w8(T+0x96,0);w8(T+0xa1,1);w8(G+0x1d,1);w8(A+0x9b,0);
    rng_result=0;dist_result=4000;sight_result=1;timer_result=0;v60_index=2;calls=0;
    res=es2_vehicle_update_registers(&memory,&g,A,entry);
    /* Lock roll succeeds (0 < 41*20 = 820): A+9Ch = 1, si = 1, A+268h = 5000. Words
     * A+259h (buf[0]) and A+265h (buf[4]) take di = 1000; buf[1] = 0; buf[2]
     * = 1 with T+A1h set -> timer A+25Eh expired -> Wp[2] = 1; V60 index 2
     * hits Wp[2] -> A+9Bh = 1, then ready_tone. */
    CHECK(r8(A+0x9c)==1&&r16(0,A+0x268)==5000&&r16(0,A+0x259)==1000&&r16(0,A+0x265)==1000);
    CHECK(r16(0,G+0xa+4)==1&&r16(0,G+0xa)==0&&r8(A+0x9b)==1&&count_calls(0x41b0bc)==1&&r8(T+0xa2)==1);
    CHECK(count_calls(0x412608)==1&&args[find(0x412608)][0]==A&&args[find(0x412608)][1]==T&&entries[find(0x412608)].eax==T);
    CHECK(args[find(0x49288c)][0]==A+0x26&&args[find(0x49288c)][1]==T+0x26&&entries[find(0x492ee0)].ecx==1);
    /* Out of the cone: all three words = di, A+9Dh cleared afterwards. */
    w16(0,A+0x10,0x9000);w8(A+0x9d,1);calls=0;
    es2_vehicle_update_registers(&memory,&g,A,entry);
    CHECK(r16(0,A+0x25c)==1000&&r16(0,A+0x25f)==1000&&r8(A+0x9d)==0);
    /* Slot-0 bodies: group_fire's pair; the pushed value dword
     * keeps the entry ECX's (411a1d) or the owner's (41194a) high word. */
    {   const uint32_t fire_entry[3]={0x11112222u,0x33334444u,0x55556666u};calls=0;
        CHECK(es2_group_fire_any_registers(&memory,&g,G,0xdead1234u,A,fire_entry)==((uint64_t)0x6b6b0000u<<32|0x5a5a0003u));
        CHECK(calls==1&&functions[0]==0x4107e4&&args[0][0]==G&&args[0][1]==0x55551234u&&args[0][2]==0xffffffffu&&fire_owner==A);
        CHECK(entries[0].eax==G&&entries[0].edx==A&&entries[0].ecx==0x55551234u);
        w8(G+0x1d,0xfe);calls=0;
        CHECK(es2_group_fire_sel_registers(&memory,&g,G,0xdead1234u,A)==((uint64_t)0x6b6b0000u<<32|0x5a5a0003u));
        CHECK(calls==1&&args[0][1]==((A&0xffff0000u)|0x1234u)&&args[0][2]==0xfffffffeu&&fire_owner==A);
        CHECK(entries[0].eax==G&&entries[0].edx==((A&0xffff0000u)|0x1234u)&&entries[0].ecx==0xfffffffeu);
        w8(G+0x1d,9);calls=0;es2_group_fire_sel_registers(&memory,&g,G,7,A);CHECK(args[0][2]==9);
    }
    printf("vehicle_update core: %u checks passed\n",checks);
    return 0;
}
