#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/pools.h"
/* Synthetic guest: objects at 90000000h, an image base B with the globals
 * the six functions read, a frame scratch at A0000000h. The callees are
 * logged; the frame setup stub writes the record head 0CAFEF00Dh. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[8192],scratch[256],globals1[0x100],globals2[0x80],globals3[0x40],globals4[0x20],globals5[0x40];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"pools line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4a9600)<sizeof(globals1))return globals1+(a-G(0x4a9600));
    if(a-G(0x4a9e00)<sizeof(globals2))return globals2+(a-G(0x4a9e00));
    if(a-G(0x4d1f40)<sizeof(globals3))return globals3+(a-G(0x4d1f40));
    if(a-G(0x6cbee0)<sizeof(globals4))return globals4+(a-G(0x6cbee0));
    if(a-G(0x4a0be0)<sizeof(globals5))return globals5+(a-G(0x4a0be0));
    if(a-G(0x4d24e0)<0x20)return globals3+0x20+(a-G(0x4d24e0));
    fprintf(stderr,"pools: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; } logged;
static logged log_[32];static unsigned calls;
static uint32_t timer_eax,alloc_result,ground_value,q10_value,step_value,virtual_value;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49630c)){CHECK(n==0);w32(0,FRAME+208-(r->eax==G(0x4981b0)?0x28:0x24),0xcafef00du);r->eax=0x1234u;}
    else if(f==G(0x467a48)||f==G(0x467aa8)){CHECK(n==1);r->eax=timer_eax;r->edx=0x5555aaaau;}
    else if(f==G(0x471b30)){CHECK(n==1);r->eax=alloc_result;}
    else if(f==G(0x46e180)||f==G(0x427d80)){r->eax=ground_value;r->edx=0x77;}
    else if(f==G(0x47e0b0)){CHECK(n==2);r->eax=q10_value;}
    else if(f==G(0x467924)){CHECK(n==1);r->eax=step_value;r->edx=0x99;}
    else if(f==0x55667788u){r->eax=virtual_value;r->edx=0x33;r->ecx=0x44;}
    else if(f==G(0x48043c)){CHECK(n==3);for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,0x100u+i);}
    else r->eax=0xabcd0000u;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={1,2,3};
static void smoke_ball_update_checks(void){
    uint32_t o=OBJ+0x100,shape=OBJ+0x200,node=OBJ+0x220,frames=OBJ+0x2a0,counts=OBJ+0x2c0;es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));
    w16(0,o+0x41,0xffff);w16(0,o+0x43,2);w16(0,o+0x45,0x8000);w32(0,o+0x26,10);w32(0,o+0x2a,20);w32(0,o+0x2e,30);
    w32(0,o+0x34,shape);w32(0,shape+4,node);w32(0,shape+8,frames);w32(0,node+0x20,counts);w16(0,counts,3);w16(0,frames,1);
    timer_eax=7;calls=0;uint64_t r=es2_smoke_ball_update_registers(&memory,&g,o,entry);
    CHECK(r==((uint64_t)0x5555aaaau<<32)&&calls==1&&log_[0].a[0]==o+0x47&&log_[0].r[1]==2&&log_[0].r[2]==0xffffffffu);
    CHECK(r32(0,o+0x26)==9&&r32(0,o+0x2a)==22&&r32(0,o+0x2e)==30u-0x8000u);
    timer_eax=0;r=es2_smoke_ball_update_registers(&memory,&g,o,entry);CHECK(r==((uint64_t)2<<32)&&r16(0,frames)==2&&r32(0,o+0x48)==200);
    r=es2_smoke_ball_update_registers(&memory,&g,o,entry);CHECK(r==1&&r16(0,frames)==0);
    /* Quotient 10000h keeps its high word: EAX = 10001h. */
    w16(0,counts,1);w16(0,frames,0xffff);r=es2_smoke_ball_update_registers(&memory,&g,o,entry);CHECK(r==0x10001u);
}
static void smoke_ball_init_checks(void){
    uint32_t ball=OBJ+0x300,point=OBJ+0x400,table=OBJ+0x500;es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));memset(globals1,0,sizeof(globals1));memset(globals4,0,sizeof(globals4));
    w32(0,G(0x4a9678),table);w32(0,table-4,0x5151u);w32(0,G(0x6cbee4),OBJ+0x600);w32(0,OBJ+0x600,9);
    w32(0,point,1);w32(0,point+4,2);w32(0,point+8,3);
    calls=0;uint64_t r=es2_smoke_ball_init_registers(&memory,&g,ball,point,0xabcdffffu,entry);
    CHECK(calls==1&&log_[0].f==G(0x402188)&&log_[0].a[0]==ball&&log_[0].a[1]==0x5151u&&log_[0].r[0]==0xffffffffu&&log_[0].r[1]==table);
    CHECK(r==((uint64_t)2<<32|ball)&&r32(0,ball)==G(0x498084)&&r16(0,ball+4)==4&&r32(0,ball+0x26)==1&&r32(0,ball+0x2e)==3&&r32(0,ball+0x48)==200&&r32(0,OBJ+0x600)==10);
}
static void smoke_update_checks(void){
    uint32_t o=OBJ+0x100,owner=OBJ+0x300,shape=OBJ+0x380,object=OBJ+0x3a0,vtable=OBJ+0x3c0,record=OBJ+0x3f0;es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));memset(globals1,0,sizeof(globals1));memset(globals4,0,sizeof(globals4));
    w32(0,G(0x6cbee4),OBJ+0x600);w32(0,G(0x4a966e),OBJ+0x700);
    w16(0,o+0x24,0);calls=0;uint64_t r=es2_smoke_update_registers(&memory,&g,o,entry);
    CHECK(r==((uint64_t)0xcafef00du<<32|1)&&calls==1&&log_[0].r[0]==G(0x497f9a)&&log_[0].r[1]==2&&log_[0].r[2]==3);
    /* Owner path: node -1, flag set; allocation fails. */
    w16(0,o+0x24,1);w16(0,o+0x19,0xffff);arena[o-OBJ+0xc]=1;w32(0,o+0x15,owner);w32(0,owner+0x26,5);w16(0,o+0x26,3);w16(0,o+0x28,2);
    timer_eax=0;alloc_result=0;calls=0;r=es2_smoke_update_registers(&memory,&g,o,entry);
    CHECK(calls==3&&log_[1].f==G(0x467a48)&&log_[1].r[0]==0x1234u&&log_[2].f==G(0x471b30)&&log_[2].r[1]==OBJ+0x700);
    CHECK(r==((uint64_t)0xcafef00du<<32|1)&&r16(0,o+0x24)==0&&r16(0,o+0x22)==200&&r16(0,o+0x26)==8);
    /* Node path with a record, euler, transform and a spawned ball. */
    w16(0,o+0x24,2);w16(0,o+0x19,3);arena[o-OBJ+0xc]=0;w32(0,owner+0x34,shape);w32(0,shape+4,object);w32(0,object,vtable);
    w32(0,vtable+0x20,0x55667788u);w32(0,shape+0x16,OBJ+0x800);w16(0,record+4,2);virtual_value=record;alloc_result=OBJ+0x900;
    calls=0;r=es2_smoke_update_registers(&memory,&g,o,entry);
    CHECK(calls==8&&log_[2].f==0x55667788u&&log_[2].a[1]==0x55550003u&&log_[3].f==G(0x4801d4)&&log_[3].a[1]==OBJ+0x840);
    CHECK(log_[4].f==G(0x47ebb8)&&log_[5].f==G(0x48043c)&&log_[7].f==G(0x409178)&&log_[7].a[0]==OBJ+0x900&&log_[7].a[2]==((OBJ+0x900)&0xffff0000u|8));
    CHECK(r16(0,owner+0x32)==1&&r32(0,FRAME+208-0x38)==0x100&&r==((uint64_t)0xcafef00du<<32));
}
static void debris_update_checks(void){
    uint32_t o=OBJ+0x100,vtable=OBJ+0x3c0;es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));memset(globals1,0,sizeof(globals1));memset(globals2,0,sizeof(globals2));memset(globals3,0,sizeof(globals3));
    w32(0,G(0x6cbee4),OBJ+0x600);w32(0,o,vtable);w32(0,vtable+0x10,0x55667788u);w16(0,G(0x4a9e4e),4);
    w32(0,o+0x2e,100);arena[o-OBJ+0x41]=0xff;step_value=0;q10_value=0;ground_value=0;virtual_value=0;
    calls=0;uint64_t r=es2_debris_update_registers(&memory,&g,o,entry);
    CHECK(r==((uint64_t)0xcafef00du<<32)&&calls==10&&log_[2].a[0]==0xfffffff6u&&log_[9].f==G(0x427d80)&&log_[9].a[0]==FRAME+208-0x44);
    /* Ground above: bounce with -q10, removal for a slow bounce. */
    w32(0,o+0x2e,0);ground_value=50;q10_value=0xfffffff0u;arena[o-OBJ+0x41]=0;
    calls=0;r=es2_debris_update_registers(&memory,&g,o,entry);
    CHECK(r==((uint64_t)0xcafef00du<<32|1)&&r16(0,o+0x4e)==0x10&&r32(0,o+0x2e)==34);
    /* Burst path: ESI = O+26 then SI = 1. */
    globals3[3]=1;w16(0,G(0x4a9654),5);w32(0,G(0x4a9650),OBJ+0xa00);w32(0,o+0x2e,0x7fffffff);w16(0,o+0x59,0xffff);
    calls=0;r=es2_debris_update_registers(&memory,&g,o,entry);
    CHECK((uint32_t)r==(((o+0x26)&0xffff0000u)|1)&&log_[calls-1].f==G(0x408530)&&log_[calls-1].a[0]==OBJ+0xa00&&r16(0,G(0x4a9668))==0x1a4);
}
static void ground_height_checks(void){
    es2_weapon_graph g=graph();w32(0,G(0x4a0be0),0x4242u);ground_value=0x1111;calls=0;
    uint64_t r=es2_ground_height_registers(&memory,&g,OBJ,entry);
    CHECK(r==((uint64_t)0x77<<32|0x1111)&&calls==1&&log_[0].a[0]==0x4242u&&log_[0].a[1]==OBJ&&log_[0].r[0]==OBJ&&log_[0].r[2]==3);
}
static void meteor_update_checks(void){
    uint32_t o=OBJ+0x100,shape=OBJ+0x200,node=OBJ+0x220,frames=OBJ+0x2a0,counts=OBJ+0x2c0,vtable=OBJ+0x3c0;es2_weapon_graph g=graph();
    memset(arena,0,sizeof(arena));memset(globals1,0,sizeof(globals1));memset(globals3,0,sizeof(globals3));memset(globals5,0,sizeof(globals5));
    w32(0,G(0x6cbee4),OBJ+0x600);w32(0,G(0x4d24ea),OBJ+0x680);
    /* Falling, under 50000: sound 2Fh, ground below. */
    w32(0,o+0x2e,1000);w16(0,o+0x49,0xfff0);ground_value=0;calls=0;
    uint64_t r=es2_meteor_update_registers(&memory,&g,o,entry);
    CHECK(calls==4&&log_[1].f==G(0x47d32c)&&log_[2].f==G(0x4628e0)&&log_[2].a[0]==0x2f&&log_[3].f==G(0x46e180)&&r32(0,o+0x2e)==1000-16&&r32(0,o+0x51)==1);
    CHECK(r==0);
    /* Hits the ground: sound 30h, radius hit. */
    ground_value=5000;calls=0;r=es2_meteor_update_registers(&memory,&g,o,entry);
    CHECK(calls==5&&log_[3].a[0]==0x30&&log_[4].f==G(0x426a20)&&log_[4].a[1]==0xbb8&&arena[o-OBJ+0x4b]==1&&r32(0,o+0x2e)==5000);
    /* Landed, animating. */
    w32(0,o+0x41,shape);w32(0,shape,vtable);w32(0,shape+4,node);w32(0,shape+8,frames);w32(0,node+0x20,counts);w16(0,counts,4);
    w32(0,o+0x4d,0xc00);step_value=0x400;calls=0;r=es2_meteor_update_registers(&memory,&g,o,entry);
    CHECK(calls==2&&r16(0,frames)==3&&r32(0,o+0x4d)==0x1000&&r==((uint64_t)0x99<<32));
    /* Done: actor placed, group cleared, flat object, destructor. */
    w32(0,o+0x55,OBJ+0x700);w32(0,OBJ+0x70c,OBJ+0x740);w32(0,OBJ+0x740,OBJ+0x780);w32(0,OBJ+0x714,1);w32(0,o+0x26,77);
    w32(0,G(0x4a9698),OBJ+0x7c0);alloc_result=OBJ+0x800;w32(0,vtable+0x18,0x55667788u);virtual_value=0x12340000u;
    calls=0;r=es2_meteor_update_registers(&memory,&g,o,entry);
    CHECK(calls==5&&r32(0,OBJ+0x780+0x26)==77&&r32(0,OBJ+0x714)==0&&r32(0,OBJ+0x800)==G(0x49a262)&&r32(0,OBJ+0x826)==77);
    CHECK(log_[4].f==0x55667788u&&log_[4].a[1]==3&&r==((uint64_t)0xcafef00du<<32|0x12340001u));
}
int main(void){
    smoke_ball_update_checks();smoke_ball_init_checks();smoke_update_checks();
    debris_update_checks();ground_height_checks();meteor_update_checks();
    printf("PASS: pools %u checks\n",checks);return 0;
}
