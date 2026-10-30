#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/lc_base.h"
/* Synthetic guest: the building, its configuration, part records, the
 * targets, the mount and weapon records and the projectile at 90000000h, an
 * image base I with the globals and tables the three functions read, frame
 * scratch at A0000000h. Every game call is logged with its arguments,
 * entry registers and the frame record; the stubs return planned values. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
static uint8_t arena[0x1000],scratch[0x100],tables[0x10],globals1[0x400],globals2[0x10],globals3[0x10];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"lc_base line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4973f0)<sizeof(tables))return tables+(a-G(0x4973f0));
    if(a-G(0x4a95b0)<sizeof(globals1))return globals1+(a-G(0x4a95b0));
    if(a-G(0x49c390)<sizeof(globals2))return globals2+(a-G(0x49c390));
    if(a-G(0x6cbee0)<sizeof(globals3))return globals3+(a-G(0x6cbee0));
    fprintf(stderr,"lc_base: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; es2_ai_call_frame fr; } logged;
static logged log_[96];static unsigned calls;
static es2_ai_call_frame frame;
static uint32_t timer_ax[16];static unsigned timers;
static uint32_t target_result,flag_result;
#define B (OBJ+0x100)
#define C (OBJ+0x400)
#define Q (OBJ+0x440)
#define P (OBJ+0x540)
#define T (OBJ+0xd00)
#define VT (OBJ+0x700)
#define W (OBJ+0x780)
#define REC (OBJ+0x800)
#define PROJ (OBJ+0xa00)
#define DEF (OBJ+0xb00)
#define POS (OBJ+0xc00)
#define ANG (OBJ+0xc10)
#define TARGET (OBJ+0xc20)
#define SLOT30 UINT32_C(0x55660030)
#define SLOT6C UINT32_C(0x5566006c)
#define SLOT54 UINT32_C(0x55660054)
#define F(x) (FRAME+0xc0u-(x))
#define PF(x) (FRAME+0x28u-(x))
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<96);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;l->fr=frame;
    if(f==G(0x467a48)){CHECK(n==1);r->eax=timer_ax[timers++%16];r->edx=0x5555aaaau;r->ecx=0x77u;}
    else if(f==G(0x403ca8)||f==G(0x403914)){CHECK(n==1);r->eax=0x0badu;r->edx=0x31u;r->ecx=0x32u;}
    else if(f==G(0x411fa0)){CHECK(n==3&&a[1]==0x10&&a[2]==0x3000);r->eax=target_result;r->edx=0x41u;r->ecx=0x42u;}
    else if(f==G(0x49288c)){CHECK(n==2);r->eax=1000;r->edx=0x51u;r->ecx=0x52u;}
    else if(f==SLOT30){CHECK(n==3);for(unsigned i=0;i<3;++i){w16(0,a[1]+2*i,0x11);w16(0,a[2]+2*i,(uint16_t)(i+1));}r->eax=0x61u;}
    else if(f==G(0x492990)){CHECK(n==3);w16(0,a[0],0x110);w16(0,a[0]+2,0x220);w16(0,a[0]+4,0x2655);r->eax=a[0];r->edx=0x71u;r->ecx=0x72u;}
    else if(f==G(0x492f24)){CHECK(n==2&&a[0]==G(0x4d2599));r->eax=0xffff0007u;r->edx=0x81u;r->ecx=0x82u;}
    else if(f==G(0x47eb30)){CHECK(n==2);w16(0,a[1],0x4000);w16(0,a[1]+2,0);w16(0,a[1]+4,0);w16(0,a[1]+6,0x4000);r->eax=0x91u;}
    else if(f==G(0x47ffe8)){CHECK(n==3);w32(0,a[2],r32(0,a[0]));w32(0,a[2]+4,r32(0,a[0]+4));r->eax=0xa1u;r->edx=0xa2u;r->ecx=0xa3u;}
    else if(f==G(0x40a9c4)){CHECK(n==5);r->eax=0xb1u;r->edx=0xb2u;r->ecx=0xb3u;}
    else if(f==G(0x40fe84)){CHECK(n==1);uint64_t v=es2_weapon_record_registers(r32(0,G(0x4a98ec)),a[0]);r->eax=(uint32_t)v;r->edx=(uint32_t)(v>>32);}
    else if(f==G(0x47ebb8)){CHECK(n==2);for(unsigned i=0;i<9;++i)w16(0,a[1]+2*i,(uint16_t)i);r->eax=0xc1u;r->edx=0xabcd00c2u;r->ecx=0xc3u;}
    else if(f==G(0x40bf74)){CHECK(n==5);r->eax=0xd1u;r->edx=0xd2u;r->ecx=0xd3u;}
    else if(f==G(0x49630c)){CHECK(n==0);w32(0,PF(0x24),0xcafef00du);r->eax=0x1234u;}
    else if(f==G(0x471b30)){CHECK(n==1);r->eax=PROJ;r->edx=0x7101u;r->ecx=0x7102u;}
    else if(f==G(0x40a948)){CHECK(n==2);r->eax=a[0];r->edx=0xe2u;r->ecx=0xe3u;}
    else if(f==G(0x49681a)){CHECK(n==3);memset(host(0,a[0]),(int)a[1],a[2]);r->eax=a[0];r->edx=a[2];r->ecx=0;}
    else if(f==SLOT6C){CHECK(n==2);r->eax=flag_result;r->edx=0xf2u;}
    else if(f==SLOT54){CHECK(n==2);r->eax=0xffff0005u;r->edx=0xf5u;}
    else if(f==G(0x40a234)){CHECK(n==2);r->eax=DEF;r->edx=0xe5u;}
    else if(f==G(0x4628e0)){CHECK(n==2);r->eax=0xe7u;r->edx=0xe8u;r->ecx=0xe9u;}
    else {fprintf(stderr,"lc_base: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(uint32_t frame_base){es2_weapon_graph g={I,frame_base,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={1,2,3};
static void world(void){
    memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));memset(tables,0,sizeof(tables));
    memset(globals1,0,sizeof(globals1));memset(globals2,0,sizeof(globals2));memset(globals3,0,sizeof(globals3));
    timers=0;for(unsigned i=0;i<16;++i)timer_ax[i]=0xffff0001u;
    w32(0,B+0x1f2,C);w32(0,B+0x205,P);w32(0,C+0x14,Q);w16(0,C+0x12,7);w16(0,B+0x10,0x1000);w16(0,B+0xc,0x10);w16(0,B+0xe,0x20);
    for(unsigned i=0;i<7;++i)w16(0,Q+0x1eu*i,10);
    w32(0,T,VT);w32(0,VT+0x30,SLOT30);w32(0,T+0x26,100);w32(0,T+0x2a,200);w32(0,T+0x2e,300);
    w32(0,G(0x4a95bc),W);w32(0,G(0x4a98ec),REC);w32(0,G(0x4a96c2),0x4a4a4a4au);w32(0,G(0x6cbee4),OBJ+0xff0);w32(0,OBJ+0xff0,40);
    w16(0,G(0x4973f4),5000);w16(0,G(0x4973f6),4000);w16(0,G(0x4973f8),3000);w16(0,G(0x4973fa),2000);
    for(unsigned j=0;j<3;++j){uint32_t w=W+0x16u*j;w16(0,w,0x2000);w16(0,w+2,0x2000);w32(0,w+4,5000);w32(0,w+8,10+j);w32(0,w+0xc,20);w32(0,w+0x10,30);w16(0,w+0x14,(uint16_t)(700+j));}
    w32(0,REC+8*0x58+0x30,0x12345678u);w16(0,REC+8*0x58+0x38,0x99);
}
static void record_checks(void){
    CHECK(es2_weapon_record_registers(0x1000,8)==((uint64_t)8<<32|(0x1000+8*0x58)));
    CHECK(es2_weapon_record_registers(0x1000,0x12340001u)==((uint64_t)1<<32|0x1058));
    CHECK(es2_weapon_record_registers(0x1000,0xffff)==((uint64_t)0xffffffffu<<32|(0x1000-0x58)));
}
static void projectile_checks(void){
    world();es2_weapon_graph g=graph(FRAME);
    uint32_t owner=B;w32(0,owner,VT);w32(0,VT+0x6c,SLOT6C);w32(0,TARGET,VT);w32(0,VT+0x54,SLOT54);
    w32(0,POS,1);w32(0,POS+4,2);w32(0,POS+8,3);w32(0,ANG,0x00220011u);w16(0,ANG+4,0x33);w16(0,DEF+0xc,0xfffe);
    w32(0,owner+0x1a4,TARGET);memset(arena+(PROJ-OBJ)+0x42,0x5a,8);
    /* Type 3 without the player byte: homing without the +6Ch call. */
    calls=0;uint64_t v=es2_projectile_create_registers(&memory,&g,&frame,0x1111u,0x2222u,3,POS,ANG,owner,0x0100,entry);
    CHECK(calls==7&&log_[0].f==G(0x49630c)&&log_[0].r[0]==G(0x498370)&&log_[0].r[1]==2&&log_[0].r[2]==3);
    CHECK(log_[0].fr.ebx==0x1111u&&log_[0].fr.esi==owner&&log_[0].fr.edi==0x2222u&&log_[0].fr.ret==G(0x40a9da));
    CHECK(log_[1].f==G(0x471b30)&&log_[1].a[0]==0x4a4a4a4au&&log_[1].r[0]==0x1234u&&log_[1].r[1]==0x4a4a4a4au);
    CHECK(log_[2].f==G(0x40a948)&&log_[2].a[0]==PROJ&&log_[2].a[1]==((PROJ&0xffff0000u)|3)&&log_[2].r[1]==PROJ&&log_[2].r[2]==0x4a4a4a4au);
    CHECK(log_[2].fr.ebx==PROJ&&r32(0,OBJ+0xff0)==35&&r16(0,PF(0x14))==8&&r32(0,PF(4))==PROJ&&r32(0,PF(0x28))==ANG);
    CHECK(log_[3].f==G(0x49681a)&&log_[3].a[0]==PROJ+0x42&&log_[3].r[1]==ANG&&log_[3].r[2]==0x00220033u);
    CHECK(log_[4].f==SLOT54&&log_[4].a[0]==TARGET&&log_[4].a[1]==PROJ+0xc&&log_[4].r[0]==PROJ+0xc&&log_[4].r[1]==VT&&log_[4].fr.edi==TARGET);
    CHECK(log_[5].f==G(0x40a234)&&log_[5].a[0]==PROJ&&log_[5].a[1]==POS&&log_[5].r[0]==B+0x92&&log_[5].r[1]==POS);
    CHECK(log_[6].f==G(0x4628e0)&&log_[6].a[0]==8&&log_[6].a[1]==POS&&log_[6].r[0]==0xa&&log_[6].r[1]==0xe5u&&log_[6].r[2]==8&&log_[6].fr.ret==G(0x40ab03));
    CHECK(v==((uint64_t)0xcafef00du<<32|0xe7u));
    CHECK(r32(0,PROJ+0x26)==1&&r32(0,PROJ+0x2a)==2&&r32(0,PROJ+0x2e)==3&&r32(0,PROJ+0xc)==0x00220011u&&r16(0,PROJ+0x10)==0x33);
    CHECK(r32(0,PROJ+0x4a)==owner&&r16(0,PROJ+0x52)==0x2f4&&r32(0,PROJ+0x56)==TARGET&&r16(0,PROJ+0x5a)==5&&r16(0,PROJ+0x54)==0&&arena[PROJ-OBJ+0x42]==0);
    CHECK(r32(0,G(0x49c394))==0);
    /* Type 0 and the player byte: +6Ch decides; flag 0 clears the target. */
    arena[owner-OBJ+0xa3]=1;flag_result=0xffff0000u;calls=0;
    v=es2_projectile_create_registers(&memory,&g,NULL,0,0,0x77770002u,POS,ANG,owner,0,entry);
    CHECK(calls==7&&log_[4].f==SLOT6C&&log_[4].a[0]==owner&&log_[4].a[1]==2&&log_[4].r[0]==owner+0x92&&log_[4].r[1]==2&&log_[4].r[2]==VT);
    CHECK(r32(0,PROJ+0x56)==0&&r32(0,G(0x49c394))==PROJ&&log_[5].f==G(0x40a234)&&log_[5].r[1]==POS);
    flag_result=2;w32(0,owner+0x1a4,0);calls=0;
    v=es2_projectile_create_registers(&memory,&g,NULL,0,0,0,POS,ANG,owner,0,entry);
    CHECK(calls==7&&r32(0,PROJ+0x56)==0&&log_[5].f==G(0x40a234));
}
static void bank_setup(void){
    /* Bank 0 retargets; its cannon and first laser fire. */
    w32(0,B,VT);target_result=T;
    timer_ax[0]=0xffff0000u; /* bank 0 target timer */
    timer_ax[1]=0xffff0000u;timer_ax[2]=0xffff0000u; /* cannon cooldown, fire */
    timer_ax[3]=0xffff0001u;timer_ax[4]=0xffff0000u; /* laser 1 */
    w16(0,B+0x209+8+6,1);
    timer_ax[5]=0xffff0001u;timer_ax[6]=0xffff0001u; /* laser 2: fire timer runs */
    timer_ax[7]=0xffff0001u;timer_ax[8]=0xffff0001u; /* banks 1, 2: no new target */
}
static void update_checks(void){
    world();es2_weapon_graph g=graph(FRAME);
    /* Dead: base_update's pair. */
    arena[B-OBJ+0x99]=1;calls=0;uint64_t v=es2_lc_base_update_registers(&memory,&g,&frame,0x5u,0x6u,B,entry);
    CHECK(calls==1&&log_[0].f==G(0x403ca8)&&log_[0].r[0]==B+0x92&&log_[0].r[1]==B+0x92&&log_[0].r[2]==3&&v==((uint64_t)0x31<<32|0x0badu));
    CHECK(log_[0].fr.esi==5&&log_[0].fr.edi==6&&log_[0].fr.ret==G(0x4045ef));
    /* Disabled: heading turned and restored, cache cleared. */
    arena[B-OBJ+0x99]=0;arena[B-OBJ+0xa5]=1;w16(0,B+0x32,1);calls=0;v=es2_lc_base_update_registers(&memory,&g,&frame,5,6,B,entry);
    CHECK(calls==1&&log_[0].f==G(0x403914)&&r16(0,B+0x10)==0x1000&&r16(0,B+0x32)==0&&v==((uint64_t)(((B+0xc)&0xffff0000u)|0x1000)<<32));
    CHECK(r16(0,F(6))==0x1000&&r16(0,F(8))==0x2555&&r32(0,F(0xc))==B+0xc&&r32(0,F(0x10))==B+0x92&&r32(0,F(0x70))==B+0xc);
    /* All weapon parts destroyed: the byte is set and the banks still run
     * (no targets, three timers). */
    arena[B-OBJ+0xa5]=0;for(unsigned i=1;i<7;++i)w16(0,P+11u*i,10);calls=0;v=es2_lc_base_update_registers(&memory,&g,&frame,5,6,B,entry);
    CHECK(arena[B-OBJ+0xa5]==1&&calls==4&&log_[1].a[0]==B+0x209+0x18&&log_[1].r[0]==0&&log_[1].r[2]==B+0x209);
    CHECK(log_[2].a[0]==B+0x228+0x18&&log_[2].r[0]==B+0xc&&r16(0,F(0x6a))==(uint16_t)(0x2555+0x5554*3)&&r16(0,B+0x10)==0x1000);
    CHECK(r32(0,F(0x14))==3&&r32(0,F(0x1c))==B+0x209+3*0x1f);
    /* Firing: part 4 still alive stops the scan. */
    world();arena[B-OBJ+0x99]=0;bank_setup();w16(0,P+11u*4,3);
    w32(0,T+0x1a2,0);w32(0,B+0x1a4,0);calls=0;timers=0;
    v=es2_lc_base_update_registers(&memory,&g,&frame,5,6,B,entry);
    CHECK(arena[B-OBJ+0xa5]==0&&r32(0,B+0x209+0x1b)==T&&r16(0,B+0x209+0x19)==10000);
    unsigned k=0;
    CHECK(log_[k].f==G(0x403914));++k;
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x209+0x18);++k;
    CHECK(log_[k].f==G(0x411fa0)&&log_[k].a[0]==B&&log_[k].r[2]==1);++k;
    CHECK(log_[k].f==G(0x49288c)&&log_[k].a[0]==B+0x26&&log_[k].a[1]==T+0x26&&log_[k].r[1]==T);++k;
    CHECK(log_[k].f==SLOT30&&log_[k].a[0]==T&&log_[k].a[1]==F(0x2c)&&log_[k].a[2]==F(0x34)&&log_[k].r[0]==B+0x209&&log_[k].r[2]==VT);++k;
    CHECK(log_[k].f==G(0x492990)&&log_[k].a[0]==F(0x3c)&&log_[k].a[1]==F(0x7c)&&log_[k].a[2]==B+0x26);++k;
    CHECK(r32(0,F(0x7c))==101&&r32(0,F(0x78))==202&&r32(0,F(0x74))==303&&r32(0,F(0x20))==1000&&r32(0,F(0x24))==T);
    CHECK(r16(0,F(0x3c))==0x100&&r16(0,F(0x3a))==0x200&&r16(0,F(0x38))==0x100);
    /* Cannon: cooldown (phase 1: 4000 + 7), fire timer, muzzle, shot. */
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x209+3&&log_[k].fr.esi==B+0x209&&log_[k].fr.edi==W);++k;
    CHECK(log_[k].f==G(0x492f24)&&log_[k].a[1]==2000&&log_[k].r[0]==1&&log_[k].r[2]==(((B+0x209+3)&0xffff0000u)|4000));++k;
    CHECK(r16(0,B+0x209+6)==1&&r16(0,B+0x209+4)==4007&&r16(0,F(0x4a))==4007);
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x209&&log_[k].r[1]==0x81u);++k;
    CHECK(log_[k].f==G(0x47eb30)&&log_[k].a[0]==0x2555&&log_[k].a[1]==F(0x54));++k;
    CHECK(log_[k].f==G(0x47ffe8)&&log_[k].a[0]==F(0x88)&&log_[k].a[1]==F(0x54)&&log_[k].a[2]==F(0xa0));++k;
    CHECK(log_[k].f==G(0x40a9c4)&&log_[k].a[0]==3&&log_[k].a[1]==F(0x94)&&log_[k].a[2]==B+0xc&&log_[k].a[3]==B&&log_[k].a[4]==0);
    CHECK(log_[k].r[0]==B+0x92&&log_[k].r[1]==B+0xc&&log_[k].r[2]==F(0x94)&&log_[k].fr.ret==G(0x4049a2));++k;
    CHECK(r32(0,F(0x94))==11&&r32(0,F(0x90))==20&&r32(0,F(0x8c))==30&&r32(0,F(0x98))==30&&r32(0,B+0x1a4)==0&&r16(0,T+0x1a2)==0);
    CHECK(arena[B-OBJ+0x9d]==1&&r32(0,F(0x58))==B+0x92&&r32(0,F(0x5c))==B+0x92&&r16(0,B+0x209+1)==700);
    /* Laser 1 (phase set in the arena): impact along the Euler matrix. */
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x209+8+3&&log_[k].fr.esi==B+0x209+8&&log_[k].fr.edi==W+0x16);++k;
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x209+8&&log_[k].r[1]==0&&log_[k].r[2]==B+0x209+8+3);++k;
    CHECK(log_[k].f==G(0x47eb30));++k;CHECK(log_[k].f==G(0x47ffe8));++k;
    CHECK(log_[k].f==G(0x492990)&&log_[k].a[0]==F(0x64)&&log_[k].a[1]==F(0x7c)&&log_[k].a[2]==F(0x94));++k;
    CHECK(log_[k].f==G(0x40fe84)&&log_[k].a[0]==8&&log_[k].r[0]==F(0x64));++k;
    CHECK(log_[k].f==G(0x47ebb8)&&log_[k].a[0]==F(0x64)&&log_[k].a[1]==F(0xc0)&&log_[k].r[0]==20&&log_[k].r[2]==F(0x64));++k;
    CHECK(log_[k].f==G(0x40bf74)&&log_[k].a[0]==3&&log_[k].a[1]==F(0xc0)&&log_[k].a[2]==0x12345678u&&log_[k].a[3]==B&&log_[k].a[4]==0xabcd0099u);
    CHECK(log_[k].r[0]==0x12345678u&&log_[k].r[1]==F(0xc0)&&log_[k].r[2]==REC+8*0x58&&log_[k].fr.ret==G(0x404a6a));++k;
    CHECK(r32(0,F(0xac))==11&&r32(0,F(0xa8))==20&&r32(0,F(0xa4))==30&&r32(0,F(0x68))==REC+8*0x58&&r16(0,B+0x209+8+1)==701);
    /* Laser 2: the fire timer runs; banks 1 and 2 keep no target. */
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x209+16+3);++k;
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x209+16);++k;
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x228+0x18&&log_[k].fr.esi==B+0x209+24&&log_[k].fr.edi==W+0x42);++k;
    CHECK(log_[k].f==G(0x467a48)&&log_[k].a[0]==B+0x247+0x18);++k;
    CHECK(calls==k&&r16(0,B+0x10)==0x1000&&r16(0,B+0x32)==0&&r32(0,F(0x44))==3&&r32(0,F(0x48))==2);
    (void)v;
}
int main(void){
    record_checks();projectile_checks();update_checks();
    printf("PASS: lc_base %u checks\n",checks);return 0;
}
