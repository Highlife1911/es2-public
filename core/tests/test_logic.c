#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/logic.h"
/* Synthetic guest: objects at 90000000h, an image base B with the few
 * globals the three functions read (pointers, camera, running minimum,
 * default node record). */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[4096],globals1[0x100],globals2[0x20],globals3[0x20],record[0x40];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"logic line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x4a9600)<sizeof(globals1))return globals1+(a-G(0x4a9600));
    if(a-G(0x4d24e0)<sizeof(globals2))return globals2+(a-G(0x4d24e0));
    if(a-G(0x6b4f30)<sizeof(globals3))return globals3+(a-G(0x6b4f30));
    if(a-G(0x6c56a0)<sizeof(record))return record+(a-G(0x6c56a0));
    fprintf(stderr,"logic: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
typedef struct logged { uint32_t f,a[3],r[3]; unsigned n; } logged;
static logged log_[16];static unsigned calls;static uint16_t timer_result;static uint32_t node_record;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16);logged *l=log_+calls++;l->f=f;l->n=n;memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x467a48)){CHECK(n==1);r->eax=(r->eax&0xffff0000u)|timer_result;}
    else if(f==G(0x4076a0)){CHECK(n==2);(void)es2_light_level_registers(&memory,B,a[0],a[1]);r->eax=0x11;r->edx=0x22;}
    else if(f==0x55667788u){CHECK(n==2);r->eax=node_record;r->edx=0x33;r->ecx=0x44;}
    else if(f==G(0x4801d4)){CHECK(n==3);w32(0,a[2],7);w32(0,a[2]+4,8);w32(0,a[2]+8,9);r->eax=0x66;r->edx=0x77;r->ecx=0x88;}
    else if(f==G(0x47ebb8)){CHECK(n==2);r->eax=0x99;}
    else if(f==G(0x48043c)){CHECK(n==3);for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,r32(0,a[0]+4*i)+100u);}
    else if(f==G(0x46297c)){CHECK(n==2&&a[0]==0x33);r->edx=0xabcdef01u;}
    else CHECK(0);
}
static es2_weapon_graph graph(uint32_t scratch){es2_weapon_graph g={B,scratch,0,0,0,call,0,0,0};return g;}
static void root_checks(void){
    static const uint32_t edges[]={0,1,2,3,4,8,9,15,16,17,24,25,0x7fffffffu,0x80000000u,0xfffe0001u,0xfffe0000u,0xffffffffu};
    for(unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);++i){uint64_t x=edges[i],r=es2_logic_root(edges[i]);CHECK(r*r<=x&&(r+1)*(r+1)>x);}
    uint32_t s=0x12345678u;for(unsigned i=0;i<200000;++i){s^=s<<13;s^=s>>17;s^=s<<5;uint32_t x=s>>(i%32);uint64_t r=es2_logic_root(x);CHECK(r*r<=x&&(r+1)*(r+1)>x);}
}
static void light_checks(void){
    uint32_t table=OBJ+0x800,light=OBJ+0x40;
    memset(arena,0,sizeof(arena));w32(0,G(0x4a9608),table);
    w16(0,light+0xc,2);uint32_t e=table+0x6c+70;w32(0,e+0x13,0x100);w32(0,e+0xf,3);
    uint64_t r=es2_light_level_registers(&memory,B,light,0xffffff7fu);
    CHECK(r16(0,e+0xd)==0x7f&&r32(0,e+0x1b)==0x7f);CHECK(r32(0,e+0x17)==0x7fu*0x2000u/10u+0x60u);CHECK(r==((uint64_t)0x60<<32|(0x7fu*0x2000u/10u+0x60u)));
    /* Negative index, a wrapped product truncated toward zero. */
    w16(0,light+0xc,0xffff);e=table+0x6c-35;w32(0,e+0x13,0x04000000u);w32(0,e+0xf,0xffffffffu);
    r=es2_light_level_registers(&memory,B,light,0xc8);
    uint32_t product=0xc8u*0x80000000u,want=(uint32_t)((int32_t)product/10)+0xffffffe0u;
    CHECK(r32(0,e+0x17)==want&&(uint32_t)r==want&&(uint32_t)(r>>32)==0xffffffe0u);
}
static void explosion_checks(void){
    uint32_t o=OBJ+0x100,types=OBJ+0x200,anims=OBJ+0x300,shape=OBJ+0x400,frames=OBJ+0x420,node=OBJ+0x440,counts=OBJ+0x460,light=OBJ+0x500,linked=OBJ+0x600;
    es2_weapon_graph g=graph(0);uint32_t entry[3]={1,2,3};
    memset(arena,0,sizeof(arena));memset(globals1,0,sizeof(globals1));
    w32(0,G(0x4a9628),types);w32(0,G(0x4a963c),anims);w32(0,G(0x4a9608),OBJ+0x800);
    arena[o-OBJ+0x41]=0xff;uint32_t t=types-40;w16(0,t,1);w16(0,t+2,0x1234);w16(0,anims+4,3);
    w32(0,o+0x34,shape);w32(0,shape+4,node);w32(0,shape+8,frames);w32(0,node+0x20,counts);w16(0,counts+6,4);w16(0,frames+6,1);
    timer_result=5;calls=0;uint64_t r=es2_explosion_update_registers(&memory,&g,o,entry);
    CHECK(r==0&&calls==1&&log_[0].a[0]==o+0x4a&&log_[0].r[0]==o+0x4a&&log_[0].r[1]==2&&log_[0].r[2]==3);
    timer_result=0;calls=0;r=es2_explosion_update_registers(&memory,&g,o,entry);
    CHECK(calls==1&&r16(0,frames+6)==2&&r==((uint64_t)t<<32));CHECK(r16(0,o+0x4b)==0x1234);
    /* Linked shape at 640h: node 660h (counts at 6c0h), frames at 6a0h. */
    w32(0,o+0x53,light);w32(0,o+0x4f,linked);w32(0,linked+0x34,OBJ+0x640);w32(0,OBJ+0x644,OBJ+0x660);w32(0,OBJ+0x648,OBJ+0x6a0);
    w32(0,OBJ+0x660+0x20,OBJ+0x6c0);w16(0,OBJ+0x6a0,2);w16(0,OBJ+0x6c0,3);arena[t-OBJ+8+6]=0x5a;
    calls=0;r=es2_explosion_update_registers(&memory,&g,o,entry);
    CHECK(calls==2&&log_[1].f==G(0x4076a0)&&log_[1].a[0]==light&&log_[1].a[1]==((t&0xffffff00u)|0x5a)&&log_[1].r[1]==3&&log_[1].r[2]==o);
    CHECK(r16(0,frames+6)==3&&r16(0,OBJ+0x6a0)==0&&r==((uint64_t)t<<32));
    calls=0;r=es2_explosion_update_registers(&memory,&g,o,entry);CHECK(calls==1&&r16(0,frames+6)==0&&r==((uint64_t)frames<<32|1));
    w16(0,anims+4,0x8000);calls=0;r=es2_explosion_update_registers(&memory,&g,o,entry);
    CHECK(calls==1&&r==((uint64_t)((t&0xffff0000u)|0x8000)<<32|((anims&0xffff0000u)|1)));
}
static void fire_checks(void){
    uint32_t o=OBJ+0x100,owner=OBJ+0x200,shape=OBJ+0x300,nodeobj=OBJ+0x340,vt=OBJ+0x380,frames=OBJ+0x3c0,counts=OBJ+0x3e0,matrices=OBJ+0x400,camera=OBJ+0x700;
    uint32_t scratch=OBJ+0xf00;es2_weapon_graph g=graph(scratch);uint32_t entry[3]={1,0x10000,3};
    memset(arena,0,sizeof(arena));memset(globals2,0,sizeof(globals2));memset(globals3,0,sizeof(globals3));
    w32(0,G(0x4d24ea),camera);w32(0,G(0x6b4f3c),0x7fffffff);
    w32(0,o+0x34,shape);w32(0,shape+8,frames);w32(0,shape+4,nodeobj);w32(0,nodeobj+0x20,counts);w16(0,counts,2);w16(0,frames,1);w16(0,o+0x59,2);
    w32(0,o+0x4a,owner);w32(0,owner+0x34,shape);w32(0,nodeobj,vt);w32(0,vt+0x20,0x55667788u);w32(0,shape+0x16,matrices);
    /* Path B: offset words through the owner's transform; frame wraps, lifetime 2 -> 1. */
    w16(0,o+0x4e,0xffff);w16(0,o+0x50,0xfffe);w16(0,o+0x52,5);w16(0,o+0x54,6);w32(0,camera+4,0xfffffffeu+100u+3u);w32(0,camera+8,105);w32(0,camera+12,106+4);
    timer_result=0;calls=0;uint64_t r=es2_fire_update_registers(&memory,&g,o,entry);
    CHECK(calls==4&&log_[1].f==G(0x47ebb8)&&log_[1].r[1]==6&&log_[1].r[2]==0xfffffffeu&&log_[2].f==G(0x48043c)&&log_[3].f==G(0x46297c));
    CHECK(r16(0,o+0x57)==0x40&&r16(0,frames)==0&&r16(0,o+0x59)==1&&r16(0,owner+0x32)==1);
    CHECK(r32(0,o+0x26)==98&&r32(0,o+0x2a)==105&&r32(0,o+0x2e)==106&&r32(0,G(0x6b4f3c))==5&&r==((uint64_t)0xabcdef01u<<32));
    /* Running timer, same distance: no sound; EDX = squared distance. */
    timer_result=7;calls=0;r=es2_fire_update_registers(&memory,&g,o,entry);CHECK(calls==2&&r==((uint64_t)25<<32));
    /* Path C: node 1 -> record with s16 index 2 -> matrix, attached byte copies its translation. */
    w16(0,o+0x4e,1);arena[o-OBJ+0x41]=1;uint32_t rec=OBJ+0x800;node_record=rec;w16(0,rec+4,2);
    w32(0,matrices+64+0x14,10);w32(0,matrices+64+0x18,20);w32(0,matrices+64+0x1c,30);w32(0,G(0x6b4f3c),0);
    calls=0;r=es2_fire_update_registers(&memory,&g,o,entry);
    CHECK(calls==3&&log_[1].f==0x55667788u&&log_[1].a[0]==nodeobj&&log_[1].a[1]==1&&log_[1].r[0]==vt);CHECK(r32(0,o+0x26)==110&&r32(0,o+0x2a)==120&&r32(0,o+0x2e)==130);
    /* Path C without a record and with 4801d4; then path A (owner position). */
    arena[o-OBJ+0x41]=0;node_record=0;w16(0,owner+0x32,0);calls=0;r=es2_fire_update_registers(&memory,&g,o,entry);
    CHECK(calls==5&&log_[2].f==G(0x4801d4)&&log_[2].a[1]==G(0x6c56a8)&&log_[2].a[0]==o+0x50&&log_[3].r[1]==0x77&&log_[3].r[2]==0x88);
    w16(0,o+0x4e,0x8000);arena[o-OBJ+0x41]=1;w32(0,owner+0x26,1);w32(0,owner+0x2a,2);w32(0,owner+0x2e,3);w16(0,o+0x59,0);
    calls=0;r=es2_fire_update_registers(&memory,&g,o,entry);CHECK(calls==1&&r32(0,o+0x26)==1&&r32(0,o+0x2e)==3&&(uint32_t)r==1);
}
int main(void){
    root_checks();light_checks();explosion_checks();fire_checks();
    puts("PASS: logic square root, light entry arithmetic, explosion and fire paths with ordered callbacks and complete pairs");printf("%u checks\n",checks);return 0;
}
