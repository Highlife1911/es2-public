#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/convoy.h"
/* Synthetic guest: objects at 90000000h, frame scratch at A0000000h, image
 * base B. The callees are logged with their slots and entry registers and
 * return planned values. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[8192],scratch[ES2_CONVOY_SCRATCH];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"convoy line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    fprintf(stderr,"convoy: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[4],r[3]; unsigned n,slot; } logged;
static logged log_[32];static unsigned calls,observed,swaps;
static uint32_t ret[32][3];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x48043c)){CHECK(n==3);for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,0x100u+i);}
    if(f==G(0x47ffe8)){CHECK(n==3);w32(0,a[2],0x11);w32(0,a[2]+4,0x2000);}
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
/* The swap 47df04 is computed in core and only observed. */
static void observe(void *c,uint32_t f,unsigned slot){(void)c;if(f==0x47df04u&&!slot){++swaps;return;}CHECK(observed==calls);log_[observed++].slot=slot;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){calls=observed=swaps=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));}
#define O (OBJ+0x100)
static void steer_checks(void){
    es2_weapon_graph g=graph();memset(arena,0,sizeof(arena));
    w16(0,O+0xc,0x1234);w16(0,O+0xe,0x5678);w16(0,O+0x10,0x0100);w16(0,O+0x32,1);arena[O-OBJ+0x227]=1;
    /* Timer fired (AX != 0), reverse flag: speed word -200 over the slot's
     * high word; steer 300h clamps to 100h over the timer's high word. */
    reset();ret[0][0]=0xabcd0001u;ret[1][1]=0x77770000u;ret[1][2]=0x88880000u;ret[2][0]=0xffff0010u;
    uint64_t p=es2_convoy_steer_registers(&memory,&g,O,0x300,0x12340005u,entry);
    CHECK(calls==3&&log_[0].f==G(0x467a48)&&log_[0].a[0]==O+0x222&&log_[0].r[0]==O+0x222&&log_[0].r[1]==entry[1]);
    CHECK(log_[1].f==G(0x467adc)&&log_[1].a[0]==O+0x220&&log_[1].a[1]==0x1234ff38u&&log_[1].a[2]==0x1e);
    CHECK(log_[1].r[0]==0xabcd0100u&&log_[1].r[1]==O+0x220&&log_[1].r[2]==O+0x222);
    CHECK(log_[2].f==G(0x47e0a0)&&log_[2].a[0]==0xc8&&log_[2].a[1]==0x100);
    CHECK(log_[2].r[0]==((O+0xc)&0xffff0000u|0x100)&&log_[2].r[1]==0x77771234u&&log_[2].r[2]==0x88885678u);
    CHECK(p==((uint64_t)0x56780110u<<32|(O+0xc))&&r16(0,O+0x10)==0x110&&r16(0,O+0x32)==0&&r16(0,O+0xc)==0x1234);
    /* Timer idle: speed slot kept; steer -300h clamps to FF00h, small
     * steer is kept as the whole dword. */
    reset();p=es2_convoy_steer_registers(&memory,&g,O,0xfffffd00u,0x12340005u,entry);
    CHECK(log_[1].a[1]==0x12340005u&&log_[2].a[1]==0xffffff00u&&log_[1].r[0]==0xff00u);
    reset();p=es2_convoy_steer_registers(&memory,&g,O,0xabcd0012u,7,entry);
    CHECK(log_[1].r[0]==0xabcd0012u&&log_[2].a[1]==0x12);
}
static void seek_checks(void){
    es2_weapon_graph g=graph();memset(arena,0,sizeof(arena));uint32_t pt=OBJ+0x400;
    w32(0,pt,0xaaaa0001u);w32(0,pt+4,2);w16(0,O+0x10,0x10);
    reset();ret[0][0]=9999;ret[2][0]=0x55550050u;ret[2][2]=0x99;ret[3][1]=0x4321;
    uint64_t p=es2_convoy_seek_registers(&memory,&g,O,pt,0x7777beefu,0,entry);
    CHECK(calls==4&&log_[0].f==G(0x4928d0)&&log_[0].a[0]==O+0x26&&log_[0].a[1]==pt&&log_[0].r[0]==O+0x26);
    CHECK(log_[1].f==G(0x4929fc)&&log_[1].a[0]==FRAME+0x34&&log_[1].a[1]==0xaaaabeefu&&log_[1].a[2]==0x2328u-9999u);
    CHECK(r32(0,FRAME+0x34)==0xaaaa0001u&&r32(0,FRAME+0x38)==2);
    CHECK(log_[2].f==G(0x492934)&&log_[2].a[0]==FRAME+0x34&&log_[2].a[1]==O+0x26&&log_[2].r[1]==FRAME+0x34);
    CHECK(log_[3].f==G(0x46a89c)&&log_[3].n==4&&log_[3].a[1]==4&&log_[3].a[2]==0x555500aau&&log_[3].a[3]==0&&log_[3].r[2]==0x99);
    CHECK(p==((uint64_t)0x4321<<32|1));
    reset();ret[0][0]=10000;ret[2][0]=0;p=es2_convoy_seek_registers(&memory,&g,O,pt,0,0x12345678u,entry);
    CHECK(log_[3].a[1]==0xffffffffu&&log_[3].a[2]==0x5678u&&(uint32_t)p==0);
}
static void lead_checks(void){
    es2_weapon_graph g=graph();memset(arena,0,sizeof(arena));uint32_t grp=OBJ+0x800;
    w32(0,O+0x45,grp);w16(0,grp+4,0xffff);
    /* No next point: steer with zeros; the index carries into the high word. */
    reset();ret[0][1]=0x21;uint64_t p=es2_convoy_lead_registers(&memory,&g,O,entry);
    CHECK(calls==2&&log_[0].a[0]==grp+4&&log_[0].a[1]==0x11120000u&&log_[0].r[0]==0x11120000u);
    CHECK(log_[1].f==G(0x46a89c)&&log_[1].a[1]==0&&log_[1].r[1]==0x21);
    w16(0,grp+4,3);reset();ret[0][0]=OBJ+0x900;ret[1][0]=OBJ+0x980;ret[2][0]=0x1234;ret[3][0]=0xffff0001u;ret[4][1]=0x66;
    p=es2_convoy_lead_registers(&memory,&g,O,entry);
    CHECK(calls==5&&log_[1].a[1]==((OBJ+0x900)&0xffff0000u|3)&&log_[2].a[0]==OBJ+0x900&&log_[2].a[1]==OBJ+0x980);
    CHECK(log_[3].f==G(0x46a958)&&log_[3].a[1]==OBJ+0x900&&log_[3].a[2]==0x1234&&log_[4].f==G(0x42313c)&&log_[4].r[1]==grp+4);
    CHECK(p==((uint64_t)0x66<<32));
}
static void follow_checks(void){
    es2_weapon_graph g=graph();memset(arena,0,sizeof(arena));uint32_t leader=OBJ+0x400,vt=OBJ+0xc00;
    w32(0,O,vt);w32(0,vt+0x78,0x55667788u);w16(0,O+0x10,0x100);w16(0,leader+0x10,0x180);
    w32(0,leader+0x26,1000);w32(0,leader+0x2a,2000);w32(0,leader+0x2e,3000);w16(0,leader+0x32,0);w16(0,leader+0x220,0x40);
    w32(0,O+0x26,10);w32(0,O+0x2a,20);w32(0,O+0x2e,30);for(unsigned i=0;i<4;++i)w16(0,leader+0x12+2*i,(uint16_t)(0x10+i));
    reset();ret[0][0]=leader;ret[3][0]=FRAME+0x38;
    uint64_t p=es2_convoy_follow_registers(&memory,&g,O,0x7abc0000u,entry);
    CHECK(calls==6&&swaps==1&&log_[0].f==G(0x46a5bc)&&log_[0].r[0]==entry[0]&&log_[1].f==0x55667788u&&log_[1].slot==0x78);
    CHECK(log_[1].a[1]==FRAME+0x24&&log_[1].r[0]==3000&&log_[1].r[2]==vt);
    CHECK(log_[2].f==G(0x47ebb8)&&log_[2].a[0]==leader+0xc&&log_[2].r[2]==leader+0x12&&r16(0,leader+0x32)==1);
    /* The swap (in core): words M+2/M+4 of M = FRAME+38h exchanged; EAX = M
     * becomes rotate_2d's second slot. */
    CHECK(r16(0,FRAME+0x38)==0x10&&r16(0,FRAME+0x3a)==0x12&&r16(0,FRAME+0x3c)==0x11&&r16(0,FRAME+0x3e)==0x13);
    CHECK(log_[3].f==G(0x47ffe8)&&log_[3].a[0]==FRAME+0xc&&log_[3].a[1]==FRAME+0x38&&r32(0,FRAME+0xc)==10u-1000u&&log_[3].r[2]==FRAME+0x38);
    /* speed 40h - (2000h >> 5) = -C0h: clamped to -96h. */
    CHECK(log_[4].f==G(0x4929fc)&&log_[4].a[1]==0x7abc0180u&&log_[4].a[2]==0x4e20&&log_[4].r[0]==0xffffff6au);
    CHECK(log_[5].f==G(0x46a958)&&log_[5].a[1]==FRAME+0x24&&log_[5].a[2]==0x7abc0180u&&log_[5].a[3]==0xffffff6au);
    CHECK(p==0);
    /* Far path: leader faces away (difference 8000h). */
    w16(0,leader+0x10,0x8100);reset();ret[0][0]=leader;ret[1][1]=0x12340000u;ret[2][0]=0x3000;ret[3][0]=0x0300;
    p=es2_convoy_follow_registers(&memory,&g,O,0x7abc0000u,entry);
    CHECK(calls==5&&log_[2].f==G(0x4928d0)&&log_[2].r[0]==0x7abc8000u&&log_[3].f==G(0x492934)&&log_[4].f==G(0x46a89c));
    CHECK(log_[4].a[1]==0x20&&log_[4].a[2]==0x100);
}
static void drive_checks(void){
    es2_weapon_graph g=graph();memset(arena,0,sizeof(arena));
    w16(0,O+0x220,0xfff0);w16(0,O+0x32,0);
    reset();ret[0][0]=O;uint64_t p=es2_convoy_drive_registers(&memory,&g,O,entry);
    CHECK(calls==4&&log_[1].f==G(0x46a9e8)&&log_[1].r[0]==O&&log_[1].r[2]==O&&log_[2].f==G(0x47ebb8)&&log_[2].r[0]==O+0x12&&log_[2].r[1]==0);
    CHECK(log_[3].f==G(0x48043c)&&log_[3].a[0]==FRAME+0x34&&r32(0,FRAME+0x38)==0xfffffff0u&&r16(0,O+0x32)==1);
    CHECK(r32(0,O+0x26)==0x100&&r32(0,O+0x2a)==0x101&&r32(0,O+0x2e)==0x102&&p==((uint64_t)0x101<<32|(O+0x26)));
    reset();ret[0][0]=OBJ;p=es2_convoy_drive_registers(&memory,&g,O,entry);
    CHECK(calls==3&&log_[1].f==G(0x46aa60));
}
static void think_checks(void){
    es2_weapon_graph g=graph();memset(arena,0,sizeof(arena));uint32_t grp=OBJ+0x800,list=OBJ+0x880,cfg=OBJ+0xa00;
    w32(0,O+0x45,grp);w16(0,grp+0x10,1);w32(0,grp+0xc,list);w32(0,list,O);w16(0,O+0x1a8,2);
    reset();ret[0][1]=0x44;uint64_t p=es2_convoy_think_registers(&memory,&g,O,entry);
    CHECK(calls==1&&log_[0].f==G(0x403ca8)&&log_[0].r[0]==(list&0xffff0000u|2)&&log_[0].r[1]==O&&log_[0].r[2]==entry[2]&&p==((uint64_t)0x44<<32));
    w16(0,O+0x1a8,3);w32(0,O+0x1f2,cfg);w16(0,cfg+0x2e,1);w16(0,O+0x20d,5);w16(0,O+0x20f,7);arena[O-OBJ+0x99]=1;
    reset();ret[0][0]=0x12340000u;ret[1][1]=0x99;p=es2_convoy_think_registers(&memory,&g,O,entry);
    CHECK(calls==2&&log_[0].f==G(0x404100)&&log_[0].r[2]==cfg&&log_[1].f==G(0x403d5c)&&log_[1].a[1]==FRAME+0x38);
    CHECK(log_[1].r[0]==0x1234fffbu&&r16(0,FRAME+0x38)==0xfffb&&r16(0,FRAME+0x3c)==7&&p==((uint64_t)0x99<<32|(O+0x92)));
    arena[O-OBJ+0x99]=0;w16(0,cfg+0x2e,0);w32(0,O+0x26,1);w32(0,O+0x2a,2);w32(0,O+0x2e,3);w16(0,O+0x10,9);w16(0,O+0x220,5);
    reset();ret[3][0]=0x10000u;p=es2_convoy_think_registers(&memory,&g,O,entry);
    CHECK(calls==4&&log_[1].f==G(0x46a810)&&log_[1].r[0]==((O+0xc)&0xffff0000u|9)&&log_[3].f==G(0x46a614)&&log_[3].r[0]==3);
    CHECK(p==0x10000u);
    w32(0,O+0x26,0x50);reset();ret[3][0]=1;p=es2_convoy_think_registers(&memory,&g,O,entry);
    CHECK(r32(0,O+0x26)==0x50&&arena[O-OBJ+0x227]==1&&r16(0,O+0x223)==3000&&r16(0,O+0x220)==0&&p==((uint64_t)2<<32|(O+0xc)));
}
/* convoy_leader 46a5bc: members 0..3 of the group, the first with
 * bytes +0A4h and +99h both 0; count words, list and group reread. */
static void leader_checks(void){
    memset(arena,0,sizeof(arena));
    const uint32_t grp=OBJ+0x1800,lst=OBJ+0x1840,m[4]={OBJ+0x100,OBJ+0x400,OBJ+0x700,OBJ+0xa00};
    w32(0,O+0x45,grp);w32(0,grp+0xc,lst);w16(0,grp+0x10,4);
    for(unsigned i=0;i<4;++i)w32(0,lst+4*i,m[i]);
    CHECK(es2_convoy_leader_registers(&memory,O)==((uint64_t)m[0]<<32|m[0]));
    arena[m[0]-OBJ+0xa4]=1;arena[m[1]-OBJ+0x99]=0x80;arena[m[2]-OBJ+0xa4]=2;arena[m[2]-OBJ+0x99]=1;
    CHECK(es2_convoy_leader_registers(&memory,O)==((uint64_t)m[3]<<32|m[3]));
    arena[m[3]-OBJ+0x99]=1;
    CHECK(es2_convoy_leader_registers(&memory,O)==((uint64_t)m[3]<<32));
    /* Count 2: member 1 alive again; the count word is signed. */
    arena[m[1]-OBJ+0x99]=0;w16(0,grp+0x10,2);
    CHECK(es2_convoy_leader_registers(&memory,O)==((uint64_t)m[1]<<32|m[1]));
}
int main(void){
    steer_checks();seek_checks();lead_checks();follow_checks();drive_checks();think_checks();leader_checks();
    printf("PASS: convoy core, %u checks\n",checks);return 0;
}
