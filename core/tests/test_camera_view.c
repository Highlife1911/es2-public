#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/camera_view.h"
/* Synthetic state for the core (outside camera and view subject). The
 * image base is placed so that the view block 4D24BCh, the subject 4D2684h,
 * the group cameras 4D2688h, the static camera 4D2754h, the guard byte
 * 49F1ECh, the counter pointer 6CBEE4h and the display pointer 4CF99Ch fall
 * inside the arena. A logging call function stands in for every game call
 * (target, arguments, count, entry EAX/EDX/ECX): multiply_q8 47e0a0 returns
 * the real Q8 product, approach_word 467adc returns scripted registers,
 * 49630c returns a fake FS:0 head and writes it to the record (f-20h), a
 * virtual slot (an address METHOD + n) writes both offset records, and every
 * other call returns scripted registers. Expected values are worked out by
 * hand from the contract in camera.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x49f000u+0x400000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define METHOD UINT32_C(0x77000000)
#define ARENA (BASE+0x2d0000u)
static uint8_t arena[0x2e0000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"camera_view line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct call { uint32_t f,arg[3],count,eax,edx,ecx; } call;
static call calls[64];static unsigned call_count,observed,scripted;
static uint32_t script_eax[8],script_edx[8],script_ecx[8];
static uint32_t frame;
static uint32_t va_of(uint32_t f){return f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<64);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->f=va_of(f);for(unsigned i=0;i<n&&i<3;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    if(f>=METHOD&&f-METHOD<0x400000u){
        /* slot +30h: attachment offset (1, 2, 3), orbit offset (4, 5, 6) */
        for(unsigned i=0;i<3;++i){w16(0,a[1]+2u*i,(uint16_t)(1u+i));w16(0,a[2]+2u*i,(uint16_t)(4u+i));}
        r->eax=0x51u;r->edx=0x52u;r->ecx=0x53u;return;
    }
    if(k->f==0x47e0a0u){const int64_t p=(int64_t)(int32_t)a[0]*(int32_t)a[1];r->eax=(uint32_t)(p>>8);r->edx=(uint32_t)((uint64_t)p>>32);return;}
    if(k->f==0x49630cu){CHECK(frame&&r->eax==VA(0x49f1fdu));w32(0,frame-0x20u,0x0badf00du);r->eax=0x0badf00du;return;}
    if(scripted<8){r->eax=script_eax[scripted];r->edx=script_edx[scripted];r->ecx=script_ecx[scripted];++scripted;}
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;++observed;}
static es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0e0e0e0u,0xe1e1e1e1u,0xabcd1234u};
static void setup(void){memset(arena,0xcc,sizeof(arena));call_count=observed=scripted=0;frame=0;graph.scratch=0;}
static void script(unsigned i,uint32_t eax,uint32_t edx,uint32_t ecx){script_eax[i]=eax;script_edx[i]=edx;script_ecx[i]=ecx;}
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}
#define B VA(0x4d24bcu)

static void test_camera_attached(void){
    uint32_t ecx=0;setup();const uint32_t C=ARENA+0x100u,O=ARENA+0x800u;
    w16(0,C+0x36u,2);w32(0,C+0x32u,O);
    uint64_t p=es2_camera_attached_registers(&memory,C,O,entry,&ecx);
    CHECK(lo(p)==1&&hi(p)==1&&ecx==entry[2]);
    p=es2_camera_attached_registers(&memory,C,O+1u,entry,NULL);CHECK(p==0);
    w16(0,C+0x36u,1);p=es2_camera_attached_registers(&memory,C,O,entry,NULL);CHECK(p==0);
    w16(0,C+0x36u,0x10002u&0xffffu);p=es2_camera_attached_registers(&memory,C,O,entry,NULL);CHECK(p==0x100000001ull);
}
static void test_camera_set(void){
    uint32_t ecx=0;setup();const uint32_t C=ARENA+0x100u,S=ARENA+0x800u,A=ARENA+0x900u;
    w32(0,A,0x22221111u);w16(0,A+4u,0x3333u);w32(0,C+0x4au,0);
    uint64_t p=es2_camera_attach_set_registers(&memory,C,S,A,entry,&ecx);
    CHECK(lo(p)==C&&hi(p)==S&&ecx==0x22223333u);
    CHECK(r32(0,C+0x3eu)==0x22221111u&&r16(0,C+0x42u)==0x3333u&&r32(0,C+0x32u)==S&&r16(0,C+0x36u)==2);
    p=es2_camera_orbit_set_registers(&memory,C,S+4u,A,entry,&ecx);
    CHECK(lo(p)==C&&hi(p)==0&&ecx==0);
    CHECK(r32(0,C+0x38u)==0x22221111u&&r16(0,C+0x3cu)==0x3333u&&r32(0,C+0x32u)==S+4u&&r16(0,C+0x36u)==1);
    CHECK(r32(0,C+0x26u)==0&&r16(0,C+0x2au)==0&&r16(0,C+0x22u)==0&&r32(0,C+0x2cu)==0&&r16(0,C+0x30u)==0&&r16(0,C+0x24u)==0);
    /* the offset over C+38h: the copied dword lands before the word is read */
    setup();w32(0,C+0x4au,0);w32(0,C+0x38u,0x55554444u);w16(0,C+0x3cu,0x6666u);
    es2_camera_orbit_set_registers(&memory,C,S,C+0x3au,entry,NULL);
    CHECK(r32(0,C+0x38u)==0x66665555u&&r16(0,C+0x3cu)==0xccccu);
    /* locked: nothing written, the entry EDX/ECX */
    setup();w32(0,C+0x4au,1);
    p=es2_camera_attach_set_registers(&memory,C,S,A,entry,&ecx);
    CHECK(lo(p)==C&&hi(p)==entry[1]&&ecx==entry[2]&&r16(0,C+0x36u)==0xccccu);
    p=es2_camera_orbit_set_registers(&memory,C,S,A,entry,&ecx);
    CHECK(lo(p)==C&&hi(p)==entry[1]&&ecx==entry[2]&&r16(0,C+0x36u)==0xccccu);
}
static void test_steps(void){
    uint32_t ecx=0;setup();const uint32_t P=ARENA+0x130u;
    script(0,0x111u,0x222u,0x333u);
    uint64_t p=es2_camera_turn_step_registers(&memory,&graph,P,0xabcdfff0u,entry,&ecx);
    CHECK(call_count==2&&observed==2&&calls[0].f==0x47e0a0u&&calls[0].arg[0]==0x800u&&calls[0].arg[1]==0xfffffff0u);
    CHECK(calls[0].eax==entry[0]&&calls[0].edx==entry[1]&&calls[0].ecx==entry[2]);
    CHECK(calls[1].f==0x467adcu&&calls[1].arg[0]==P&&calls[1].arg[1]==0xffffff80u&&calls[1].arg[2]==0xc0u&&calls[1].eax==P&&calls[1].edx==0xffffffffu);
    CHECK(lo(p)==0x111u&&hi(p)==0x222u&&ecx==0x333u);
    setup();script(0,1,2,3);
    es2_camera_speed_step_registers(&memory,&graph,P,0x10u,entry,NULL);
    CHECK(calls[0].arg[0]==0x400u&&calls[0].arg[1]==0x10u&&calls[1].arg[1]==0x40u&&calls[1].arg[2]==0x50u);
}
static void test_outside_camera(void){
    uint32_t ecx=0;setup();const uint32_t C=ARENA+0x100u;
    w32(0,C+0x4au,0);w16(0,C+0x36u,0);
    for(unsigned i=0;i<3;++i)script(i,0x100u+i,0x200u+i,0x300u+i);
    uint64_t p=es2_outside_camera_registers(&memory,&graph,C,0x12340005u,0x00000007u,0x10000u,entry,&ecx);
    /* c's low word is 0: speed to 0, pitch delta to +b */
    CHECK(call_count==3&&calls[0].f==0x401c20u&&calls[0].arg[0]==C+0x30u&&calls[0].arg[1]==0xfffffffbu);
    CHECK(calls[0].eax==0xfffffffbu&&calls[0].edx==C+0x30u&&calls[0].ecx==entry[2]);
    CHECK(calls[1].f==0x401c4cu&&calls[1].arg[0]==C+0x24u&&calls[1].arg[1]==0&&calls[1].eax==C+0x24u&&calls[1].edx==0x200u&&calls[1].ecx==0x300u);
    CHECK(calls[2].f==0x401c20u&&calls[2].arg[0]==C+0x2cu&&calls[2].arg[1]==7u&&calls[2].eax==0x101u&&calls[2].edx==0x201u);
    CHECK(lo(p)==0x102u&&hi(p)==0x202u&&ecx==0x302u);
    setup();w32(0,C+0x4au,0);w16(0,C+0x36u,1);
    es2_outside_camera_registers(&memory,&graph,C,0xffff0005u,0x0001fff9u,1u,entry,NULL);
    CHECK(calls[0].arg[1]==5u&&calls[1].arg[1]==0xfff9u&&calls[2].arg[1]==0);
    setup();w32(0,C+0x4au,0);w16(0,C+0x36u,0);
    es2_outside_camera_registers(&memory,&graph,C,0,0x0001fff9u,1u,entry,NULL);
    CHECK(calls[0].arg[1]==0xffff0000u&&calls[1].arg[1]==7u&&calls[2].arg[1]==0);
    setup();w32(0,C+0x4au,0);w16(0,C+0x36u,5);
    p=es2_outside_camera_registers(&memory,&graph,C,1,2,3,entry,&ecx);
    CHECK(call_count==0&&lo(p)==1u&&hi(p)==entry[1]&&ecx==entry[2]);
    setup();w32(0,C+0x4au,0);w16(0,C+0x36u,0xffffu);
    p=es2_outside_camera_registers(&memory,&graph,C,1,2,3,entry,NULL);
    CHECK(call_count==0&&lo(p)==0xfffffffbu);
    setup();w32(0,C+0x4au,7);
    p=es2_outside_camera_registers(&memory,&graph,C,1,2,3,entry,&ecx);
    CHECK(call_count==0&&lo(p)==entry[0]&&hi(p)==entry[1]&&ecx==entry[2]);
}
static void view_block(uint32_t camera,uint32_t display){
    w32(0,VA(0x4cf99cu),display);w32(0,B+0x2eu,camera);w32(0,camera+0x4au,0);
    w32(0,B+0x2au,ARENA+0x400u);w32(0,B+0x60u,ARENA+0x500u);w16(0,B+0x64u,0);
}
static void test_view_apply(void){
    uint32_t ecx=0;setup();const uint32_t C=ARENA+0x100u,D=ARENA+0x700u;
    view_block(C,D);arena[D+0x1cu-BASE]=1;w16(0,B+0x5eu,2);w16(0,B+0x32u,0);w16(0,B+0x78u,0);
    script(0,0x41u,0x42u,0x43u);
    uint64_t p=es2_view_apply_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==1&&calls[0].f==0x45df34u&&calls[0].arg[0]==ARENA+0x400u&&calls[0].ecx==ARENA+0x400u);
    CHECK(calls[0].eax==((D&0xffff0000u)|2u)&&calls[0].edx==calls[0].eax);
    CHECK(r16(0,B+0x32u)==2&&r16(0,C+0x36u)==2&&r16(0,B+0x34u)==0);
    CHECK(lo(p)==C&&hi(p)==0x42u&&ecx==ARENA+0x400u);
    /* 3: orbit around [B+60h], countdown 2, then one step of 45e208 */
    setup();view_block(C,D);arena[D+0x1cu-BASE]=1;w16(0,B+0x5eu,3);w16(0,B+0x32u,0);
    script(0,1,2,3);script(1,4,5,6);
    p=es2_view_apply_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==2&&calls[0].arg[0]==ARENA+0x500u&&calls[0].edx==ARENA+0x500u&&calls[0].ecx==0);
    CHECK(calls[1].f==0x45e208u&&calls[1].count==0&&calls[1].eax==C&&calls[1].ecx==ARENA+0x500u);
    CHECK(r16(0,C+0x36u)==1&&r16(0,B+0x64u)==1&&lo(p)==4u&&hi(p)==5u&&ecx==6u);
    /* 1: free flight; locked camera keeps its mode */
    setup();view_block(C,D);arena[D+0x1cu-BASE]=1;w16(0,B+0x5eu,1);w16(0,B+0x32u,4);w16(0,B+0x5cu,0x77u);w32(0,C+0x4au,1);
    p=es2_view_apply_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==0&&r16(0,C+0x36u)==0xccccu&&r16(0,B+0x34u)==0x77u&&lo(p)==C&&hi(p)==((D&0xffff0000u)|0x77u)&&ecx==0);
    /* 6: the view word is taken, nothing else */
    setup();view_block(C,D);arena[D+0x1cu-BASE]=1;w16(0,B+0x5eu,6);w16(0,B+0x32u,4);
    p=es2_view_apply_registers(&memory,&graph,entry,&ecx);
    CHECK(call_count==0&&r16(0,B+0x32u)==6&&ecx==2u);
    /* no transition: view 3 with the player as subject refreshes B+34h */
    setup();view_block(C,D);arena[D+0x1cu-BASE]=0;w16(0,B+0x32u,3);w32(0,VA(0x4d2684u),ARENA+0x400u);
    w16(0,B+0x34u,1);w16(0,B+0x5cu,9);
    p=es2_view_apply_registers(&memory,&graph,entry,&ecx);
    CHECK(r16(0,B+0x34u)==9&&lo(p)==((D&0xffff0000u)|9u)&&hi(p)==ARENA+0x400u&&ecx==((entry[2]&0xffff0000u)|1u));
}
static void subject_setup(unsigned count,uint32_t current){
    const uint32_t P=ARENA+0x400u,G=ARENA+0x480u;
    w32(0,VA(0x4d24e6u),P);w32(0,P+0x45u,G);w16(0,G+0x10u,(uint16_t)count);
    for(unsigned i=0;i<4;++i){const uint32_t K=ARENA+0x1000u+0x60u*i;w32(0,VA(0x4d2688u)+4u*i,K);w32(0,K+0x32u,ARENA+0x900u+i);w32(0,K+0x4au,0);}
    w32(0,VA(0x4d24eau),current);w32(0,current+0x4au,0);
    arena[VA(0x49f1ecu)-BASE]=1;w16(0,VA(0x4d2518u),0x44u);
    graph.scratch=ARENA+0x1800u;frame=graph.scratch+ES2_VIEW_SUBJECT_FRAME;
}
static void test_view_subject_set(void){
    uint32_t ecx=0;setup();const uint32_t C=ARENA+0x100u,S=ARENA+0x902u;
    /* the subject's own group camera (index 2) */
    subject_setup(4,C);
    uint64_t p=es2_view_subject_set_registers(&memory,&graph,S,entry,&ecx);
    const uint32_t K2=ARENA+0x10c0u;
    CHECK(call_count==1&&calls[0].f==0x49630cu&&calls[0].edx==entry[1]&&calls[0].ecx==entry[2]);
    CHECK(r32(0,VA(0x4d24eau))==K2&&r16(0,K2+0x36u)==1&&r32(0,VA(0x4d2684u))==S&&r16(0,VA(0x4d24f0u))==1);
    CHECK(lo(p)==((K2&0xffff0000u)|1u)&&hi(p)==0x0badf00du&&ecx==S);
    /* a free group camera (index 1): fields copied, then view_offsets(S) */
    setup();subject_setup(4,C);w32(0,ARENA+0x1060u+0x32u,0);
    for(unsigned i=4;i<0x4e;++i)arena[C+i-BASE]=(uint8_t)(0x40u+i);
    w32(0,C+0x4au,0);
    script(0,0x61u,0x62u,0x63u);
    p=es2_view_subject_set_registers(&memory,&graph,S,entry,&ecx);
    const uint32_t K1=ARENA+0x1060u;
    CHECK(call_count==2&&calls[1].f==0x45edb0u&&calls[1].arg[0]==S&&calls[1].eax==K1&&calls[1].edx==S&&calls[1].ecx==r32(0,C+8u));
    for(unsigned i=4;i<0x4e;++i)CHECK(arena[K1+i-BASE]==arena[C+i-BASE]);
    CHECK(r32(0,VA(0x4d24eau))==K1&&lo(p)==((0x61u&0xffff0000u)|1u)&&ecx==S);
    /* no group camera at all (count 0): the static camera; the player as subject */
    setup();subject_setup(0,C);
    for(unsigned i=4;i<0x4e;++i)arena[C+i-BASE]=(uint8_t)(0x80u+i);
    w32(0,C+0x4au,0);
    script(0,0x71u,0x72u,0x73u);
    p=es2_view_subject_set_registers(&memory,&graph,ARENA+0x400u,entry,&ecx);
    const uint32_t T=VA(0x4d2754u);
    CHECK(call_count==2&&calls[1].f==0x45edb0u&&calls[1].eax==ARENA+0x400u&&calls[1].edx==r32(0,C+0xcu)&&calls[1].ecx==r32(0,C+4u));
    for(unsigned i=4;i<0x4e;++i)CHECK(arena[T+i-BASE]==arena[C+i-BASE]);
    CHECK(r32(0,VA(0x4d24eau))==T&&r16(0,VA(0x4d24f0u))==0x44u&&lo(p)==0x44u);
    /* first call: the static camera is constructed; negative count: no camera change */
    setup();subject_setup(0xffffu,C);arena[VA(0x49f1ecu)-BASE]=0;
    const uint32_t N=ARENA+0x1a00u;w32(0,VA(0x6cbee4u),N);w32(0,N,10);
    script(0,0x81u,0x82u,0x83u);
    p=es2_view_subject_set_registers(&memory,&graph,S,entry,&ecx);
    CHECK(call_count==2&&calls[1].f==0x48bd84u&&calls[1].arg[0]==T&&calls[1].eax==0x0badf00du);
    CHECK(r32(0,T)==VA(0x49f8e8u)&&r32(0,T+0x32u)==0&&r32(0,T+0x4au)==0&&r16(0,T+0x36u)==0&&r16(0,T+0x44u)==0&&r16(0,T+0x46u)==0&&r16(0,T+0x48u)==0);
    CHECK(r32(0,N)==8u&&arena[VA(0x49f1ecu)-BASE]==1&&r16(0,frame-0x10u)==8u);
    CHECK(r32(0,VA(0x4d24eau))==C&&lo(p)==0xffff0001u&&hi(p)==0x0badf00du);
}
static void test_view_offsets(void){
    uint32_t ecx=0;setup();const uint32_t C=ARENA+0x100u,S=ARENA+0x800u,V=ARENA+0x880u;
    w32(0,VA(0x4d24eau),C);w32(0,S,V);w32(0,V+0x30u,METHOD+1u);
    graph.scratch=ARENA+0x1800u;const uint32_t f=graph.scratch+ES2_VIEW_OFFSETS_FRAME;
    script(0,0x91u,0x92u,0x93u);script(1,0xa1u,0xa2u,0xa3u);
    uint64_t p=es2_view_offsets_registers(&memory,&graph,S,entry,&ecx);
    CHECK(call_count==3&&observed==3&&calls[0].f==METHOD+1u&&calls[0].arg[0]==S&&calls[0].arg[1]==f-8u&&calls[0].arg[2]==f-0x10u);
    CHECK(calls[0].eax==f-0x10u&&calls[0].edx==f-8u&&calls[0].ecx==V);
    CHECK(calls[1].f==0x40111cu&&calls[1].arg[0]==C&&calls[1].arg[1]==S&&calls[1].arg[2]==f-8u&&calls[1].eax==f-8u&&calls[1].edx==C&&calls[1].ecx==0x53u);
    CHECK(calls[2].f==0x40109cu&&calls[2].arg[2]==f-0x10u&&calls[2].eax==C&&calls[2].edx==0x92u&&calls[2].ecx==f-0x10u);
    CHECK(r16(0,f-8u)==1&&r16(0,f-0xcu)==6&&lo(p)==0xa1u&&hi(p)==0xa2u&&ecx==0xa3u);
}
static void test_camera_current(void){
    uint32_t ecx=0;setup();w32(0,VA(0x4d24eau),0x12345678u);
    const uint64_t p=es2_camera_current_registers(&memory,IMAGE,entry,&ecx);
    CHECK(lo(p)==0x12345678u&&hi(p)==entry[1]&&ecx==entry[2]);
}
int main(void){
    test_camera_attached();test_camera_set();test_steps();test_outside_camera();test_view_apply();
    test_view_subject_set();test_view_offsets();test_camera_current();
    printf("PASS: camera_view %u checks\n",checks);return 0;
}
