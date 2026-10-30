#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapons.h"
#define BASE UINT32_C(0x90000000)
static uint8_t arena[4096];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"weapons line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static unsigned calls,mode;static uint32_t seen[5],functions[10];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<10);functions[calls++]=f;
    if(f==1){CHECK(n==1);r->eax=0xffff;r->edx=0x12345678;}
    else if(f==2){CHECK(n==2);r->eax=mode?10:9;r->edx=0x11111111;r->ecx=0xcafe1234;}
    else if(f==3){CHECK(n==5);memcpy(seen,a,20);r->eax=0x12345678;r->edx=0x87654321;}
    else if(f==4){CHECK(n==2);(void)es2_mask_merge_registers(&memory,a[0],a[1]);}
    else if(f==5){CHECK(n==1);CHECK(r->edx==BASE+0x130);w16(0,a[0]+1,123);r->edx=0xaabbccdd;}
    else if(f==6){CHECK(n==3);w32(0,BASE+0x11c,BASE+0x600);r->eax=0xdeadbeef;r->edx=0xcafe4321;}
    else if(f==7){CHECK(n==1);w32(0,BASE+0x120,BASE+0x700);r->eax=0xfeed1234;r->edx=0xbeef5678;}
    else if(f==8){CHECK(n==5);memcpy(seen,a,20);r->eax=0;r->edx=0xfedcba98;}
    else if(f==9){CHECK(n==4);memcpy(seen,a,16);r->eax=0x12341234;r->edx=0x87658765;}
    else CHECK(0);
}
/* Graph callees: multiply_q8 (47e0a0) returns the product's Q8,
 * movement_step (467924) its argument plus 1, dist_gate (49288c) a
 * planned distance per call. */
static uint32_t gfunctions[16],gargs[16][2],gentry[16][3],distances[16];static unsigned gcalls;
static void gcall(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(gcalls<16);gfunctions[gcalls]=f;for(unsigned i=0;i<n&&i<2;++i)gargs[gcalls][i]=a[i];
    gentry[gcalls][0]=r->eax;gentry[gcalls][1]=r->edx;gentry[gcalls][2]=r->ecx;
    if(f==0x47e0a0){CHECK(n==2);r->eax=(uint32_t)((int32_t)a[0]*(int32_t)a[1]>>8);}
    else if(f==0x467924){CHECK(n==1);r->eax=a[0]+1;}
    else if(f==0x49288c){CHECK(n==2);r->eax=distances[gcalls];}
    else CHECK(0);
    r->edx=0xd0d00000u+gcalls;r->ecx=0xc0c00000u+gcalls;++gcalls;
}
static es2_weapon_context context(void){es2_weapon_context g={BASE,BASE+4,2,4,5,6,8,9,BASE+0x800,BASE+0x810,call,0,0,0};return g;}
int main(void){
    for(unsigned index=0;index<256;++index){uint32_t value=0xfffffff0u+14u*(uint32_t)(index<128?(int32_t)index:(int32_t)index-256);uint64_t want=(uint64_t)value<<32|value;CHECK(es2_projectile_def_registers(0xfffffff0u,(uint8_t)index)==want);CHECK(es2_bullet_def_registers(0xfffffff0u,(uint8_t)index)==want);}
    for(int delta=-15;delta<=15;++delta){uint8_t expected[64];for(unsigned i=0;i<64;++i)arena[i]=expected[i]=(uint8_t)(i*31+3);for(unsigned i=0;i<8;++i)expected[24+i]&=expected[24+delta+(int)i];uint64_t r=es2_mask_merge_registers(&memory,BASE+24,BASE+24+(uint32_t)delta);CHECK(r==(((uint64_t)(BASE+32)<<32)|(BASE+32+(uint32_t)delta)));CHECK(!memcmp(expected,arena,64));}
    es2_weapon_context g=context();uint32_t entry[3]={1,0x55667788,3};
    memset(arena,0,sizeof(arena));w32(0,BASE,BASE+0x20);w16(0,BASE+4,1);w32(0,BASE+0x20,BASE+0x100);w32(0,BASE+0x100,BASE+0x400);w32(0,BASE+0x145,BASE+0x500);w32(0,BASE+0x45c,1);w32(0,BASE+0x470,3);
    calls=0;mode=0;uint64_t r=es2_radius_hit_registers(&memory,&g,BASE+0x600,11,0xabcd9876,0x55556666,0,entry);
    CHECK(calls==3&&functions[0]==1&&functions[1]==2&&functions[2]==3);CHECK(seen[1]==0x55559876&&seen[3]==0xcafe000b&&seen[4]==0x55556666);CHECK(r==UINT64_C(0x8765432112340001));
    calls=0;mode=1;r=es2_radius_hit_registers(&memory,&g,BASE+0x600,11,0,0,0,entry);CHECK(calls==2&&r==UINT64_C(0xffffffff00000000));
    calls=0;r=es2_radius_hit_registers(&memory,&g,BASE+0x600,11,0,0,BASE+0x100,entry);CHECK(!calls&&(uint32_t)(r>>32)==BASE+0x500);
    uint32_t w=BASE+0x100;memset(arena,0,sizeof(arena));w32(0,w+0x10,BASE+0x500);w32(0,w+0x14,BASE+0x550);w32(0,BASE+0x520,BASE+0x580);w16(0,BASE+0x550,65535);w16(0,BASE+0x580,65535);arena[0x144]=1;memset(arena+0x133,0xf0,8);memset(arena+0x13b,0xcc,8);
    calls=0;r=es2_weapon_step_registers(&memory,&g,w,0x4321,entry);CHECK(calls==2&&functions[0]==4&&functions[1]==5);CHECK(r==UINT64_C(0x0000000100004321));CHECK(r16(0,BASE+0x550)==1&&arena[0x144]==1);for(unsigned i=0;i<8;++i)CHECK(arena[0x133+i]==0xc0&&arena[0x13b+i]==0);
    arena[0x149]=1;calls=0;r=es2_weapon_step_registers(&memory,&g,w,0x4321,entry);CHECK(!calls&&r==((uint64_t)entry[1]<<32|((w+0x43)&0xffff0000u)|0x4321));
    memset(arena,0,sizeof(arena));w32(0,w+0x20,BASE+0x650);w16(0,BASE+0x652,0x8001);w32(0,BASE+0x300,BASE+0x400);w32(0,BASE+0x438,7);w16(0,w+0x7b,8);w16(0,BASE+0x638,8);arena[0x14c]=1;
    calls=0;r=es2_weapon_fire_registers(&memory,&g,w,BASE+0x300,0,entry);CHECK(calls==3&&functions[0]==6&&functions[1]==7&&functions[2]==8);CHECK(seen[0]==0xbeef8001&&seen[4]==0xfeed1234);CHECK(r==UINT64_C(0xfedcba9800000000));CHECK(!r16(0,w+0x7b)&&!arena[0x14c]);
    /* missile_steer: camera byte set, axes 2 and -1 (500h Q8: 10 and
     * -5, plus 1 by the scripted time scale): O+10h -= 11, O+0Ch += -4. */
    {es2_weapon_graph gg={0x400000u,0,0,0,0,gcall,0,0,0};const uint32_t O=BASE+0x200,R=BASE+0x300,ax=BASE+0x380,ay=BASE+0x384;
    memset(arena,0,sizeof(arena));w16(0,O+0xc,100);w16(0,O+0xe,200);w16(0,O+0x10,300);w16(0,O+0x32,1);
    arena[0x30d]=1;w32(0,R+0x22,ax);w32(0,R+0x26,ay);w16(0,ax,2);w16(0,ay,0xffff);gcalls=0;
    r=es2_missile_steer_registers(&memory,&gg,O,R,entry);
    CHECK(gcalls==4&&gfunctions[0]==0x47e0a0&&gargs[0][0]==0x500&&gargs[0][1]==2&&gentry[0][0]==((O+0xc)&0xffff0000u)+300);
    CHECK(gentry[0][1]==ax&&gentry[0][2]==((entry[2]&0xffff0000u)|200)&&gargs[1][0]==10&&gargs[3][0]==0xfffffffbu);
    CHECK(r16(0,O+0x10)==289&&r16(0,O+0xc)==96&&r16(0,O+0xe)==200&&!r16(0,O+0x32)&&!arena[0x30d]&&!r16(0,ax)&&!r16(0,ay));
    CHECK(r==((uint64_t)ax<<32));
    /* Without the camera byte the guidance stops. */
    w32(0,O+0x56,7);arena[0x241]=3;gcalls=0;r=es2_missile_steer_registers(&memory,&gg,O,R,entry);
    CHECK(!gcalls&&!r32(0,O+0x56)&&!arena[0x241]&&r==((uint64_t)entry[1]<<32|entry[0]));
    /* ally_nearest: members X, a down one (+0A4h), two live ones at 5000
     * and 4000; the nearer wins, EDX = dist_gate's of the last pass. */
    const uint32_t G=BASE+0x400,list=BASE+0x420,X=BASE+0x500,M1=BASE+0x600,M2=BASE+0x700,M3=BASE+0x800;
    w32(0,G+0xc,list);w16(0,G+0x10,4);w32(0,list,X);w32(0,list+4,M1);w32(0,list+8,M2);w32(0,list+12,M3);arena[0x6a4]=1;
    gcalls=0;distances[0]=5000;distances[1]=4000;r=es2_ally_nearest_registers(&memory,&gg,G,X,entry);
    CHECK(gcalls==2&&gargs[0][0]==M2+0x26&&gargs[0][1]==X+0x26&&gentry[0][0]==X&&gentry[0][1]==X+0x26&&gentry[0][2]==M2+0x26);
    CHECK(r==((uint64_t)0xd0d00001u<<32|M3));
    /* Nobody under 100,000; an empty group returns 0 with the entry EDX. */
    gcalls=0;distances[0]=100000;distances[1]=200000;CHECK((uint32_t)es2_ally_nearest_registers(&memory,&gg,G,X,entry)==0);
    w16(0,G+0x10,0x8000);gcalls=0;CHECK(es2_ally_nearest_registers(&memory,&gg,G,X,entry)==((uint64_t)entry[1]<<32)&&!gcalls);}
    puts("PASS: weapons mapped guest memory, alias order, signed thresholds, callback mutations and complete pairs");printf("%u checks\n",checks);return 0;
}
