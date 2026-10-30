#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/destructors.h"
/* Synthetic guest: an arena at 90000000h (objects, shape instances, their
 * vtables, two counters, two removal lists), a frame scratch at A0000000h,
 * an image base I with the globals 6cbee4 (counter pointer), 4a9691 and
 * 4a9602 (removal lists), 6b4f38 (fire count). Every game call is logged
 * with its arguments, its entry registers and the context word f-14h; the
 * stubs answer with fixed registers per callee. Expectations from the
 * reference listing (main_loop.md, section "object destructors"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_DESTRUCTORS_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_DESTRUCTORS_SCRATCH],globals[4][4];
static const uint32_t global_va[4]={0x6cbee4,0x4a9691,0x4a9602,0x6b4f38};
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"destructors line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    for(unsigned i=0;i<4;++i)if(a-G(global_va[i])<4u)return globals[i]+(a-G(global_va[i]));
    fprintf(stderr,"destructors: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define O (OBJ+0x100)
#define S (OBJ+0x300)
#define SVT (OBJ+0x400)
#define SDTOR UINT32_C(0x55500018)
#define COUNTER (OBJ+0x500)
#define COUNTER2 (OBJ+0x510)
#define LIST1 (OBJ+0x600)
#define LIST2 (OBJ+0x640)
#define BLOCK (OBJ+0x700)
#define HEAD UINT32_C(0x0012ff00)
typedef struct logged { uint32_t f,a[2],r[3],context; unsigned n; } logged;
static logged log_[16];static unsigned calls;
static int frame_open,switch_counter;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<16);logged *l=log_+calls++;memset(l,0,sizeof(*l));l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(frame_open)l->context=r16(0,F(0x14));
    if(f==G(0x49630c)){ /* the record at EBP+[descriptor+4] = f-24h */
        CHECK(n==0);frame_open=1;w32(0,F(0x24),HEAD);w16(0,F(0x14),0);r->eax=HEAD;}
    else if(f==G(0x496406)){CHECK(n==2&&a[1]==0);r->eax=5;r->edx=0x60000001u;}
    else if(f==SDTOR){CHECK(n==2&&a[1]==3);r->eax=0x61000001u;r->edx=0x61000002u;r->ecx=0x61000003u;}
    else if(f==G(0x477548)){CHECK(n==1);r->eax=0x62000001u;r->edx=0x62000002u;r->ecx=0x62000003u;}
    else if(f==G(0x47758c)){CHECK(n==1);r->eax=0x63000001u;r->edx=0x63000002u;r->ecx=0x63000003u;}
    else if(f==G(0x478628)){CHECK(n==2);r->eax=0x64000001u;r->edx=0x64000002u;r->ecx=0x64000003u;
        if(switch_counter)w32(0,G(0x6cbee4),COUNTER2);}
    else if(f==G(0x462ac4)){CHECK(n==1&&a[0]==0x33);r->eax=0x65000001u;r->edx=0x65000002u;r->ecx=0x65000003u;}
    else if(f==G(0x48be20)){CHECK(n==2&&a[1]==0);r->eax=0x66000001u;r->edx=0x66000002u;r->ecx=0x66000003u;}
    else {fprintf(stderr,"destructors: unexpected call %08x\n",f);exit(1);}
}
static unsigned observed[16],observed_slot[16],observations;
static void observe(void *c,uint32_t f,unsigned slot){(void)c;if(observations<16){observed[observations]=f;observed_slot[observations]=slot;}++observations;}
static es2_weapon_graph graph(void){
    es2_weapon_graph g={I,FRAME,0,0,0,call,NULL,observe,NULL};return g;
}
static void reset(uint32_t shape){
    memset(arena,0x5a,sizeof(arena));memset(scratch,0,sizeof(scratch));calls=0;frame_open=0;switch_counter=0;observations=0;
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,100);w32(0,COUNTER2,200);
    w32(0,G(0x4a9691),LIST1);w32(0,G(0x4a9602),LIST2);w32(0,G(0x6b4f38),2);
    w32(0,O+0x34,shape);w32(0,S,SVT);w32(0,SVT+0x18,SDTOR);w32(0,S+8,BLOCK);
}
static const uint32_t entry[3]={0xe0000001u,0xe0000002u,0xe0000003u};
/* The shape part of a tail: 496406, then the virtual with the context word 14h. */
static void check_shape_calls(unsigned i,uint32_t context_before){
    CHECK(log_[i].f==G(0x496406)&&log_[i].a[0]==S&&log_[i].context==context_before);
    CHECK(log_[i+1].f==SDTOR&&log_[i+1].a[0]==S&&log_[i+1].context==0x14);
}
static void test_variants(void){
    es2_weapon_graph g=graph();uint32_t ecx,head;
    /* DEBRIS, flag 2 (no free), with a shape: counter -2, +5 (RTTI), -2. */
    reset(S);
    uint64_t v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_DEBRIS,O,2,entry,&ecx,&head);
    CHECK(calls==3&&log_[0].f==G(0x49630c)&&log_[0].r[0]==G(0x498a18)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].r[0]==S&&log_[1].r[1]==S&&log_[1].r[2]==COUNTER);
    check_shape_calls(1,0);
    CHECK(log_[2].r[0]==S&&log_[2].r[1]==SVT&&log_[2].r[2]==COUNTER);
    CHECK(r32(0,COUNTER)==100-2+5-2&&r32(0,O)==G(0x4a0b80)&&r32(0,F(4))==S&&r16(0,F(0x14))==8);
    CHECK(head==HEAD&&v==((uint64_t)HEAD<<32|COUNTER)&&ecx==COUNTER);
    CHECK(observations==3&&observed[2]==SDTOR&&observed_slot[2]==0x18&&observed[1]==0x496406&&observed_slot[1]==0);
    /* DEBRIS, null object: only the record. */
    reset(S);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_DEBRIS,0,3,entry,&ecx,&head);
    CHECK(calls==1&&r32(0,COUNTER)==100&&v==((uint64_t)HEAD<<32|HEAD)&&ecx==entry[2]);
    /* METEOR, no shape, flag 1: free(O), ECX = O. */
    reset(0);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_METEOR,O,1,entry,&ecx,&head);
    CHECK(calls==2&&log_[1].f==G(0x477548)&&log_[1].a[0]==O&&log_[1].r[0]==COUNTER&&log_[1].r[2]==COUNTER);
    CHECK(r32(0,COUNTER)==96&&v==((uint64_t)HEAD<<32|0x62000001u)&&ecx==O);
    /* BULLET with a shape, flag 2: three decrements first; ECX = head. */
    reset(S);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_BULLET,O,2,entry,&ecx,&head);
    CHECK(calls==3&&log_[0].r[0]==G(0x498579));
    CHECK(log_[1].r[0]==COUNTER&&log_[1].r[1]==S&&log_[1].r[2]==S);
    CHECK(log_[2].r[0]==S&&log_[2].r[1]==COUNTER&&log_[2].r[2]==SVT);
    CHECK(r32(0,COUNTER)==100-3+5-2&&v==((uint64_t)COUNTER<<32|COUNTER)&&ecx==HEAD&&head==HEAD);
    /* ROCKET, null object: EDX = entry. */
    reset(S);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_ROCKET,0,1,entry,&ecx,&head);
    CHECK(calls==1&&r32(0,COUNTER)==100&&v==((uint64_t)entry[1]<<32|HEAD)&&ecx==HEAD);
    /* FlatObj frees after the shape: free's EAX, ECX = O. */
    reset(S);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_FLAT,O,3,entry,&ecx,&head);
    CHECK(calls==4&&log_[3].f==G(0x477548)&&log_[3].r[0]==COUNTER&&log_[3].r[2]==COUNTER&&log_[3].context==8);
    CHECK(v==((uint64_t)HEAD<<32|0x62000001u)&&ecx==O);
}
static void test_explosion_fire_beam(void){
    es2_weapon_graph g=graph();uint32_t ecx,head;
    /* EXPLOSION: two appends, the counter pointer switched by the first. */
    reset(S);w32(0,O+0x4f,0x11111111u);w32(0,O+0x53,0x22222222u);switch_counter=1;
    uint64_t v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_EXPLOSION,O,0,entry,&ecx,&head);
    CHECK(calls==5&&log_[0].r[0]==G(0x497c28));
    CHECK(log_[1].f==G(0x478628)&&log_[1].a[0]==LIST1&&log_[1].a[1]==0x11111111u&&log_[1].r[0]==0x11111111u&&log_[1].r[1]==LIST1&&log_[1].r[2]==entry[2]);
    CHECK(log_[2].f==G(0x478628)&&log_[2].a[0]==LIST2&&log_[2].a[1]==0x22222222u&&log_[2].r[2]==0x64000003u);
    CHECK(log_[3].r[0]==S&&log_[3].r[1]==0x64000002u&&log_[3].r[2]==COUNTER2);
    check_shape_calls(3,0);
    CHECK(log_[4].r[0]==SVT&&log_[4].r[1]==COUNTER2&&log_[4].r[2]==S);
    CHECK(r32(0,COUNTER)==99&&r32(0,COUNTER2)==200-1+5-2&&r32(0,O)==G(0x4a0b80));
    CHECK(v==((uint64_t)COUNTER2<<32|HEAD)&&ecx==COUNTER2&&head==HEAD);
    /* EXPLOSION: first link zero, second nonzero; null object exits after one decrement. */
    reset(0);w32(0,O+0x4f,0);w32(0,O+0x53,7);
    v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_EXPLOSION,O,0,entry,&ecx,&head);
    CHECK(calls==2&&log_[1].a[0]==LIST2&&log_[1].a[1]==7&&r32(0,COUNTER)==96);
    reset(S);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_EXPLOSION,0,1,entry,&ecx,&head);
    CHECK(calls==1&&r32(0,COUNTER)==99&&v==((uint64_t)COUNTER<<32|HEAD)&&ecx==entry[2]);
    /* FIRE: count 2 -> 1 no sound; 1 -> 0 sfx_stop(33h). */
    reset(0);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_FIRE,O,0,entry,&ecx,&head);
    CHECK(calls==1&&r32(0,G(0x6b4f38))==1&&r32(0,O)==G(0x4a0b80)&&r32(0,COUNTER)==96);
    reset(0);w32(0,G(0x6b4f38),1);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_FIRE,O,0,entry,&ecx,&head);
    CHECK(calls==2&&log_[1].f==G(0x462ac4)&&log_[1].r[0]==HEAD&&log_[1].r[1]==COUNTER&&log_[1].r[2]==entry[2]&&r32(0,G(0x6b4f38))==0);
    CHECK(v==((uint64_t)COUNTER<<32|HEAD)&&ecx==COUNTER);
    /* BEAM: arena free of O+58h first. */
    reset(S);w32(0,O+0x58,0x77777777u);v=es2_object_dtor_registers(&memory,&g,ES2_DTOR_BEAM,O,1,entry,&ecx,&head);
    CHECK(calls==5&&log_[1].f==G(0x47758c)&&log_[1].a[0]==0x77777777u&&log_[1].r[2]==0x77777777u&&log_[1].r[1]==COUNTER);
    CHECK(log_[2].r[0]==S&&log_[2].r[1]==COUNTER&&log_[2].r[2]==0x77777777u);
    CHECK(log_[4].f==G(0x477548)&&log_[4].a[0]==O&&log_[4].r[1]==COUNTER&&log_[4].r[2]==COUNTER);
    CHECK(r32(0,COUNTER)==100-3+5-2&&v==((uint64_t)0x62000002u<<32|HEAD)&&ecx==O);
}
static void test_shapes(void){
    es2_weapon_graph g=graph();uint32_t ecx;
    reset(0);uint64_t v=es2_shape_dtor_registers(&memory,&g,S,3,entry,&ecx);
    CHECK(calls==3&&log_[0].f==G(0x47758c)&&log_[0].a[0]==BLOCK&&log_[0].r[0]==COUNTER&&log_[0].r[1]==BLOCK&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x48be20)&&log_[1].a[0]==S&&log_[1].r[0]==0x63000001u&&log_[1].r[2]==BLOCK);
    CHECK(log_[2].f==G(0x477548)&&log_[2].a[0]==S&&log_[2].r[2]==0x66000003u);
    CHECK(r32(0,S)==G(0x4a62ed)&&r32(0,COUNTER)==99&&v==((uint64_t)0x62000002u<<32|0x62000001u)&&ecx==S);
    reset(0);v=es2_shape_dtor_registers(&memory,&g,S,2,entry,&ecx);
    CHECK(calls==2&&v==((uint64_t)0x66000002u<<32|0x66000001u)&&ecx==0x66000003u);
    reset(0);v=es2_shape_dtor_registers(&memory,&g,0,1,entry,&ecx);
    CHECK(calls==0&&r32(0,COUNTER)==99&&v==((uint64_t)entry[1]<<32|COUNTER)&&ecx==entry[2]);
    reset(0);v=es2_shape_base_dtor_registers(&memory,&g,S,0,entry,&ecx);
    CHECK(calls==0&&r32(0,S)==G(0x4a6a14)&&r32(0,COUNTER)==99&&v==((uint64_t)COUNTER<<32|S)&&ecx==entry[2]);
    reset(0);v=es2_shape_base_dtor_registers(&memory,&g,S,0xff01,entry,&ecx);
    CHECK(calls==1&&log_[0].f==G(0x477548)&&log_[0].r[0]==S&&log_[0].r[1]==COUNTER&&log_[0].r[2]==entry[2]);
    CHECK(v==((uint64_t)0x62000002u<<32|0x62000001u)&&ecx==S);
    reset(0);v=es2_shape_base_dtor_registers(&memory,&g,0,1,entry,&ecx);
    CHECK(calls==0&&v==((uint64_t)COUNTER<<32|0)&&ecx==entry[2]);
}
int main(void){
    test_variants();
    test_explosion_fire_beam();
    test_shapes();
    printf("destructors: %u checks passed\n",checks);
    return 0;
}
