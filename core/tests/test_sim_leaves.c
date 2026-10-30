#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/sim_leaves.h"
/* Synthetic state for the core (the last simulation leaves). The
 * image base is placed so that every global the functions touch (4975d6 ..
 * 4a9e5e) falls inside the arena. A logging call function stands in for
 * every game call (target, arguments, count, entry EAX/EDX/ECX); an
 * address METHOD + n is a vtable target that returns n, and every other
 * call returns the scripted registers. Expected values are worked out by
 * hand from the contract in vtables.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-(0x4975d6u-0x400000u))
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define METHOD UINT32_C(0x77000000)
static uint8_t arena[0x20000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"sim_leaves line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct call { uint32_t f,arg[2],count,eax,edx,ecx,g04; } call;
static call calls[16];static unsigned call_count,observed,scripted;
static uint32_t script_eax[8],script_edx[8],script_ecx[8];
static uint32_t va_of(uint32_t f){return f>=IMAGE&&f-IMAGE<0x300000u?f-IMAGE+0x400000u:f;}
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<16);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->f=va_of(f);for(unsigned i=0;i<n&&i<2;++i)k->arg[i]=a[i];
    k->count=n;k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;k->g04=r32(0,VA(0x4a5b04u));
    if(f>=METHOD&&f-METHOD<0x400000u){r->eax=f-METHOD;r->edx=0x5000u+call_count;r->ecx=0x6000u+call_count;return;}
    if(scripted<8){r->eax=script_eax[scripted];r->edx=script_edx[scripted];r->ecx=script_ecx[scripted];++scripted;}
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;++observed;}
#define SCRATCH (BASE+0x7000u)
static es2_weapon_graph graph={IMAGE,SCRATCH,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0e0e0e0u,0xe1e1e1e1u,0xabcd1234u};
static void setup(void){memset(arena,0xcc,sizeof(arena));call_count=observed=scripted=0;}
static void script(unsigned i,uint32_t eax,uint32_t edx,uint32_t ecx){script_eax[i]=eax;script_edx[i]=edx;script_ecx[i]=ecx;}
static uint32_t lo(uint64_t p){return (uint32_t)p;}
static uint32_t hi(uint64_t p){return (uint32_t)(p>>32);}

/* part_shapes_bind: two non-empty slots of A take B's first entry and, for
 * B's zero entry, the second pool record; ECX advances 18h per B entry
 * taken; the loop ends when A's count runs out, EAX = 0 and EDX the low
 * word of the last B element address replaced by the wrapped count. */
static void test_part_shapes_bind(void){
    uint32_t ecx=0;setup();
    const uint32_t A=BASE+0x2000u,B=BASE+0x2010u,AR=BASE+0x2100u,BR=BASE+0x2200u,POOL=BASE+0x3000u;
    const uint32_t X0=BASE+0x2300u,X1=BASE+0x2310u,S0=BASE+0x2400u,S1=BASE+0x2420u,REC=BASE+0x2500u;
    w32(0,A,AR);w16(0,A+4u,2);w32(0,B,BR);w16(0,B+4u,2);
    w32(0,AR,X0);w32(0,AR+4u,X1);w32(0,X0,S0);w32(0,X1,S1);
    w16(0,S0+4u,0x1111u);w16(0,S0+6u,0x2222u);w16(0,S1+4u,0x3333u);w16(0,S1+6u,0x4444u);
    w32(0,BR,REC);w32(0,BR+4u,0);
    uint64_t p=es2_part_shapes_bind_registers(&memory,A,B,POOL,entry,&ecx);
    CHECK(r16(0,REC+4u)==0x1111u&&r16(0,REC+6u)==0x2222u&&r32(0,X0)==REC);
    CHECK(r16(0,POOL+0x18u+4u)==0x3333u&&r16(0,POOL+0x18u+6u)==0x4444u&&r32(0,X1)==POOL+0x18u);
    CHECK(ecx==POOL+0x30u&&lo(p)==0&&hi(p)==((POOL+0x18u)&0xffff0000u));
    /* an empty slot of A (zero element) skips the pair but still advances ECX */
    setup();w32(0,A,AR);w16(0,A+4u,1);w32(0,B,BR);w16(0,B+4u,1);w32(0,AR,0);w32(0,BR,REC);w16(0,REC+4u,0x5555u);
    p=es2_part_shapes_bind_registers(&memory,A,B,POOL,entry,&ecx);
    CHECK(ecx==POOL+0x18u&&r16(0,REC+4u)==0x5555u&&lo(p)==0);
    /* no entries in A: nothing happens, ECX = the pool */
    setup();w32(0,A,AR);w16(0,A+4u,0);w32(0,B,BR);w16(0,B+4u,5);
    p=es2_part_shapes_bind_registers(&memory,A,B,POOL,entry,&ecx);
    CHECK(ecx==POOL&&lo(p)==0&&hi(p)==(BR&0xffff0000u));
}
/* parts_intact: the first part whose cap word is >= 0 and whose current word
 * is below the cap ends the walk; EAX 1 only when the walk completes. */
static void test_parts_intact(void){
    uint32_t ecx=0;setup();
    const uint32_t O=BASE+0x3000u,CFG=BASE+0x3100u,REC=BASE+0x3800u,EB=BASE+0x3a00u;
    w32(0,O+0x1f2u,CFG);w32(0,O+0x205u,EB);w16(0,CFG+0x12u,3);w32(0,CFG+0x14u,REC);
    w16(0,REC,10);w16(0,REC+6u,5);w16(0,EB,10);
    w16(0,REC+0x1eu,100);w16(0,REC+0x1eu+6u,0xffffu);w16(0,EB+0x0bu,1);
    w16(0,REC+0x3cu,7);w16(0,REC+0x3cu+6u,0);w16(0,EB+0x16u,3);
    uint64_t p=es2_parts_intact_registers(&memory,O,entry,&ecx);
    CHECK(lo(p)==0&&hi(p)==2&&ecx==3);
    w16(0,EB+0x16u,7);
    p=es2_parts_intact_registers(&memory,O,entry,&ecx);
    CHECK(lo(p)==1&&hi(p)==3&&ecx==3);
    w16(0,CFG+0x12u,0xffffu);                    /* a negative count: the walk is empty, EAX = 0 */
    p=es2_parts_intact_registers(&memory,O,entry,&ecx);
    CHECK(lo(p)==0&&hi(p)==0&&ecx==0xffffffffu);
}
/* formation_slot: the first call clears the cell, the table entry is E + 10 x
 * col - 10 for E = [[4a95ac]+28 x row+4]. */
static void test_formation_slot(void){
    uint32_t ecx=0;setup();
    const uint32_t T=BASE+0x4000u,E=BASE+0x4400u;
    w32(0,VA(0x4a95acu),T);w32(0,T+28u*2u+4u,E);w8(VA(0x4975e2u),0);
    w32(0,VA(0x4975d6u),0x11u);w32(0,VA(0x4975deu),0x77u);
    w32(0,E+20u,0xd1d1d1d1u);w32(0,E+24u,0xd2d2d2d2u);
    uint64_t p=es2_formation_slot_registers(&memory,IMAGE,0xabcd0002u,0x00010003u,entry,&ecx);
    CHECK(lo(p)==VA(0x4975d6u)&&hi(p)==0xd1d1d1d1u&&ecx==30u);
    CHECK(r32(0,VA(0x4975d6u))==0xd1d1d1d1u&&r32(0,VA(0x4975dau))==0xd2d2d2d2u&&r32(0,VA(0x4975deu))==0&&r8(VA(0x4975e2u))==1);
    /* the byte is set: the cell is kept; a negative col goes below the entry base */
    w32(0,VA(0x4975deu),0x77u);w32(0,E-20u,0xe1e1e1e1u);w32(0,E-16u,0xe2e2e2e2u);
    p=es2_formation_slot_registers(&memory,IMAGE,2,0xffffu,entry,&ecx);
    CHECK(hi(p)==0xe1e1e1e1u&&ecx==0xfffffff6u&&r32(0,VA(0x4975deu))==0x77u&&r32(0,VA(0x4975dau))==0xe2e2e2e2u&&r8(VA(0x4975e2u))==1);
}
static void test_explosion_entry(void){
    uint32_t ecx=0;setup();
    const uint32_t O=BASE+0x5000u;w32(0,VA(0x4a9628u),0x1000u);w8(O+0x41u,0xffu);
    uint64_t p=es2_explosion_entry_registers(&memory,IMAGE,O,entry,&ecx);
    CHECK(lo(p)==0x1000u-40u&&hi(p)==0x1000u-40u&&ecx==entry[2]);
    w8(O+0x41u,3);p=es2_explosion_entry_registers(&memory,IMAGE,O,entry,&ecx);
    CHECK(lo(p)==0x1000u+120u&&hi(p)==lo(p));
}
/* flat_draw: terrain_pose with the entry registers, then shape_actor_matrix
 * with its EAX/EDX and ECX = O, then the child's slot +1Ch with
 * (child, vtable, O); the last ECX is the child. */
static void test_flat_draw(void){
    uint32_t ecx=0;setup();
    const uint32_t O=BASE+0x5000u,C=BASE+0x5100u,V=BASE+0x5200u;
    w32(0,O+0x34u,C);w32(0,C,V);w32(0,V+0x1cu,METHOD+7u);
    script(0,0x11,0x22,0x33);script(1,0x44,0x55,0x66);
    uint64_t p=es2_flat_draw_registers(&memory,&graph,O,entry,&ecx);
    CHECK(call_count==3&&observed==3);
    CHECK(calls[0].f==0x4029d8u&&calls[0].arg[0]==O&&calls[0].eax==entry[0]&&calls[0].edx==entry[1]&&calls[0].ecx==entry[2]);
    CHECK(calls[1].f==0x401fe4u&&calls[1].arg[0]==O&&calls[1].eax==0x11&&calls[1].edx==0x22&&calls[1].ecx==O);
    CHECK(calls[2].f==METHOD+7u&&calls[2].arg[0]==C&&calls[2].count==1&&calls[2].eax==C&&calls[2].edx==V&&calls[2].ecx==O);
    CHECK(lo(p)==7&&hi(p)==0x5003u&&ecx==C);
}
/* projectile_draw: 4a5b04 = 0 and EAX = 0 into shape_actor_matrix (entry EDX
 * and ECX), the child's slot +1Ch with (EAX of the matrix call, child,
 * vtable), then 4a5b04 = EAX = [4a5b08]. */
static void test_projectile_draw(void){
    uint32_t ecx=0;setup();
    const uint32_t O=BASE+0x5000u,C=BASE+0x5100u,V=BASE+0x5200u;
    w32(0,O+0x34u,C);w32(0,C,V);w32(0,V+0x1cu,METHOD+9u);
    w32(0,VA(0x4a5b04u),0x99u);w32(0,VA(0x4a5b08u),0x55u);
    script(0,0x66,0x77,0x88);
    uint64_t p=es2_projectile_draw_registers(&memory,&graph,O,entry,&ecx);
    CHECK(call_count==2&&observed==2);
    CHECK(calls[0].f==0x401fe4u&&calls[0].arg[0]==O&&calls[0].eax==0&&calls[0].edx==entry[1]&&calls[0].ecx==entry[2]&&calls[0].g04==0);
    CHECK(calls[1].f==METHOD+9u&&calls[1].arg[0]==C&&calls[1].eax==0x66&&calls[1].edx==C&&calls[1].ecx==V);
    CHECK(lo(p)==0x55&&hi(p)==0x5002u&&ecx==C&&r32(0,VA(0x4a5b04u))==0x55u);
}
/* beam_done: 1 exactly when the low word of timer_word_step's EAX is zero. */
static void test_beam_done(void){
    uint32_t ecx=0;setup();
    const uint32_t O=BASE+0x5000u;
    script(0,0xabcd0000u,0x77,0);
    uint64_t p=es2_beam_done_registers(&memory,&graph,O,entry,&ecx);
    CHECK(call_count==1&&calls[0].f==0x467a48u&&calls[0].arg[0]==O+0x5cu&&calls[0].eax==entry[0]&&calls[0].edx==entry[1]&&calls[0].ecx==entry[2]);
    CHECK(lo(p)==1&&hi(p)==0x77&&ecx==O+0x5cu);
    setup();script(0,0x00000001u,0x78,0);
    p=es2_beam_done_registers(&memory,&graph,O,entry,&ecx);
    CHECK(lo(p)==0&&hi(p)==0x78&&ecx==O+0x5cu);
}
/* weapon_by_mount: the first listed entry whose definition byte matches the
 * low word of w (sign-extended byte), EDX = the index, ECX = the last
 * pointer loaded. */
static void test_weapon_by_mount(void){
    uint32_t ecx=0;setup();
    const uint32_t L=BASE+0x5000u,P=BASE+0x5100u,C1=BASE+0x5200u,C2=BASE+0x5300u,D1=BASE+0x5400u,D2=BASE+0x5500u;
    w32(0,L+4u,P);w16(0,L+8u,3);w32(0,P,0);w32(0,P+4u,C1);w32(0,P+8u,C2);
    w32(0,C1+0x18u,D1);w32(0,C2+0x18u,D2);w8(D1+0x17u,0xfeu);w8(D2+0x17u,5);
    uint64_t p=es2_weapon_by_mount_registers(&memory,L,0xabcd0005u,entry,&ecx);
    CHECK(lo(p)==C2&&hi(p)==2&&ecx==C2);
    p=es2_weapon_by_mount_registers(&memory,L,0xfffeu,entry,&ecx);
    CHECK(lo(p)==C1&&hi(p)==1&&ecx==C1);
    p=es2_weapon_by_mount_registers(&memory,L,9,entry,&ecx);
    CHECK(lo(p)==0&&hi(p)==3&&ecx==C2);
    w16(0,L+8u,0);
    p=es2_weapon_by_mount_registers(&memory,L,5,entry,&ecx);
    CHECK(lo(p)==0&&hi(p)==0&&ecx==entry[2]);
}
static void test_ptr_list_add(void){
    uint32_t ecx=0;setup();
    const uint32_t L=BASE+0x5000u,ARR=BASE+0x5100u,P=BASE+0x5200u;
    w32(0,L,ARR);w16(0,L+6u,2);
    uint64_t p=es2_ptr_list_add_registers(&memory,L,P,entry,&ecx);
    CHECK(r32(0,ARR+8u)==P&&r16(0,L+6u)==3&&r16(0,P+0x4bu)==2&&lo(p)==2&&hi(p)==P&&ecx==2);
    w16(0,L+6u,0xffffu);
    p=es2_ptr_list_add_registers(&memory,L,P,entry,&ecx);
    CHECK(r32(0,ARR-4u)==P&&r16(0,L+6u)==0&&r16(0,P+0x4bu)==0xffffu&&lo(p)==0xffffffffu&&ecx==0xffffffffu);
}
static void test_box_square_init(void){
    uint32_t ecx=0;setup();
    const uint32_t R=BASE+0x5000u;
    uint64_t p=es2_box_square_init_registers(&memory,R,0x1234fffeu,entry,&ecx);
    CHECK(r16(0,R+6u)==0xfffeu&&r16(0,R+8u)==0xfffeu&&r16(0,R+4u)==0x200u&&r16(0,R+2u)==0xffffu&&r16(0,R)==0xffffu);
    CHECK(lo(p)==R&&hi(p)==((entry[1]&0xffff0000u)|0xfffeu)&&ecx==0xffffffffu);
    p=es2_box_square_init_registers(&memory,R,7,entry,&ecx);
    CHECK(r16(0,R)==3&&r16(0,R+2u)==3&&ecx==3&&hi(p)==((entry[1]&0xffff0000u)|7u));
}
/* The two sound latches are the same code on other cells. */
typedef uint64_t (*latch_fn)(const es2_guest_memory *,const es2_weapon_graph *,uint32_t,uint32_t,const uint32_t [3],uint32_t *);
static void test_latch(latch_fn fn,uint32_t flag_va,uint32_t word_va,uint32_t offset){
    uint32_t ecx=0;setup();
    const uint32_t V=BASE+0x5000u,flag=VA(flag_va),word=VA(word_va);
    /* flag clear, |w| above 0C0h, latch word differs: sfx_playing(21h) */
    w8(flag,0);w16(0,word,5);w16(0,V+offset,9);script(0,0,0x31,0x32);
    uint64_t p=fn(&memory,&graph,V,0x100u,entry,&ecx);
    CHECK(call_count==1&&calls[0].f==0x462af0u&&calls[0].arg[0]==0x21u&&calls[0].eax==0x100u&&calls[0].edx==((entry[1]&0xffff0000u)|5u)&&calls[0].ecx==entry[2]);
    CHECK(r8(flag)==1&&r16(0,word)==9&&lo(p)==0&&hi(p)==0x31&&ecx==((0x21u&0xffff0000u)|9u));
    /* a nonzero answer keeps the flag clear; a negative w is negated */
    setup();w8(flag,0);w16(0,word,5);w16(0,V+offset,9);script(0,1,0x31,0);
    p=fn(&memory,&graph,V,0xff00u,entry,&ecx);
    CHECK(calls[0].eax==0x100u&&r8(flag)==0&&r16(0,word)==9&&lo(p)==1);
    /* at the threshold or with equal words: no call */
    setup();w8(flag,0);w16(0,word,5);w16(0,V+offset,9);
    p=fn(&memory,&graph,V,0xc0u,entry,&ecx);
    CHECK(call_count==0&&r8(flag)==0&&r16(0,word)==9&&lo(p)==0xc0u&&hi(p)==entry[1]&&ecx==((entry[2]&0xffff0000u)|9u));
    setup();w8(flag,0);w16(0,word,9);w16(0,V+offset,9);
    p=fn(&memory,&graph,V,0x7fffu,entry,&ecx);
    CHECK(call_count==0&&r8(flag)==0&&hi(p)==((entry[1]&0xffff0000u)|9u));
    /* flag set: AX = 0 clears it; equal words clear it; different words keep it */
    setup();w8(flag,1);w16(0,word,5);w16(0,V+offset,9);
    p=fn(&memory,&graph,V,0u,entry,&ecx);
    CHECK(call_count==0&&r8(flag)==0&&r16(0,word)==9&&lo(p)==0);
    setup();w8(flag,1);w16(0,word,9);w16(0,V+offset,9);
    p=fn(&memory,&graph,V,0x100u,entry,&ecx);
    CHECK(r8(flag)==0&&lo(p)==9);
    setup();w8(flag,1);w16(0,word,5);w16(0,V+offset,9);
    p=fn(&memory,&graph,V,0x100u,entry,&ecx);
    CHECK(r8(flag)==1&&lo(p)==5&&call_count==0&&r16(0,word)==9);
    /* 8000h negates to itself, which is signed below 0C0h: no call */
    setup();w8(flag,0);w16(0,word,5);w16(0,V+offset,9);
    p=fn(&memory,&graph,V,0x8000u,entry,&ecx);
    CHECK(call_count==0&&lo(p)==0x8000u);
}
static void test_type_index(void){
    uint32_t ecx=0;setup();
    w8(VA(0x4a9d08u)+3u,0x81u);w8(VA(0x4a9d08u)-2u,0x42u);w8(VA(0x4a9dccu)+3u,0x13u);w8(VA(0x4a9dccu)-2u,0x14u);
    uint64_t p=es2_mech_type_index_registers(&memory,IMAGE,0x70003u,entry,&ecx);
    CHECK(lo(p)==0x81u&&hi(p)==entry[1]&&ecx==entry[2]);
    p=es2_mech_type_index_registers(&memory,IMAGE,0xfffeu,entry,&ecx);
    CHECK(lo(p)==0x42u);
    p=es2_flyer_type_index_registers(&memory,IMAGE,3,entry,&ecx);
    CHECK(lo(p)==0x13u&&hi(p)==entry[1]&&ecx==entry[2]);
    p=es2_flyer_type_index_registers(&memory,IMAGE,0xfffeu,entry,&ecx);
    CHECK(lo(p)==0x14u);
}
static void test_notice_time(void){
    uint32_t ecx=0;setup();
    w16(0,VA(0x4a9e5eu),1);
    uint64_t p=es2_notice_time_registers(&memory,IMAGE,entry,&ecx);
    CHECK(lo(p)==0x7530u&&hi(p)==entry[1]&&ecx==entry[2]);
    w16(0,VA(0x4a9e5eu),0);p=es2_notice_time_registers(&memory,IMAGE,entry,&ecx);
    CHECK(lo(p)==0xea60u);
    w16(0,VA(0x4a9e5eu),0xffffu);p=es2_notice_time_registers(&memory,IMAGE,entry,&ecx);
    CHECK(lo(p)==0xea60u);
}
/* cockpit_notice / radio_notice: the 24-byte record at the graph's scratch,
 * the port's slot 0 with (port, record). */
static void test_notices(void){
    uint32_t ecx=0;setup();
    const uint32_t OBJ=BASE+0x5000u,PORT=BASE+0x5100u,VT=BASE+0x5200u,A=BASE+0x5300u;
    w32(0,VA(0x49b088u),OBJ);w32(0,OBJ+0x20bu,PORT);w32(0,OBJ+0x207u,PORT);w32(0,PORT,VT);w32(0,VT,METHOD+3u);
    memset(arena+(SCRATCH-BASE),0xee,24);
    uint64_t p=es2_cockpit_notice_registers(&memory,&graph,0x12340021u,entry,&ecx);
    CHECK(call_count==1&&observed==1&&calls[0].f==METHOD+3u&&calls[0].count==2&&calls[0].arg[0]==PORT&&calls[0].arg[1]==SCRATCH);
    CHECK(calls[0].eax==OBJ&&calls[0].edx==PORT&&calls[0].ecx==VT);
    CHECK(r16(0,SCRATCH)==0x21u&&r32(0,SCRATCH+2u)==0&&r32(0,SCRATCH+6u)==0&&r16(0,SCRATCH+22u)==0&&lo(p)==3&&hi(p)==0x5001u&&ecx==0x6001u);
    /* radio_notice: held back (byte A+99h and a zero flag word) returns (A, A+92h) at once */
    setup();w32(0,VA(0x49b088u),OBJ);w32(0,OBJ+0x207u,PORT);w32(0,PORT,VT);w32(0,VT,METHOD+4u);w8(A+0x99u,1);
    p=es2_radio_notice_registers(&memory,&graph,A,5,0xabcd0000u,entry,&ecx);
    CHECK(call_count==0&&lo(p)==A&&hi(p)==A+0x92u&&ecx==entry[2]);
    /* flag nonzero: port_a answers the port; slot 0 gets (port, record) with EAX = vtable, EDX = record */
    script(0,PORT,A+0x92u,0x1234);
    p=es2_radio_notice_registers(&memory,&graph,A,5,1,entry,&ecx);
    CHECK(call_count==2&&observed==2&&calls[0].f==0x433158u&&calls[0].count==0&&calls[0].eax==A&&calls[0].edx==A+0x92u&&calls[0].ecx==((entry[2]&0xffff0000u)|5u));
    CHECK(calls[1].f==METHOD+4u&&calls[1].arg[0]==PORT&&calls[1].arg[1]==SCRATCH&&calls[1].eax==VT&&calls[1].edx==SCRATCH&&calls[1].ecx==0x1234u);
    CHECK(r16(0,SCRATCH)==5&&r32(0,SCRATCH+2u)==A&&lo(p)==4&&hi(p)==0x5002u&&ecx==0x6002u);
    /* byte clear: the record is posted whatever the flag is */
    setup();w32(0,VA(0x49b088u),OBJ);w32(0,OBJ+0x207u,PORT);w32(0,PORT,VT);w32(0,VT,METHOD+6u);w8(A+0x99u,0);script(0,PORT,0,0);
    p=es2_radio_notice_registers(&memory,&graph,A,0xfff0007u,0,entry,&ecx);
    CHECK(call_count==2&&r16(0,SCRATCH)==7&&lo(p)==6);
}
static uint64_t twist(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t v,uint32_t w,const uint32_t e[3],uint32_t *c){
    return es2_twist_sound_latch_registers(m,g,v,w,e,c);}
static uint64_t pitch(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t v,uint32_t w,const uint32_t e[3],uint32_t *c){
    return es2_pitch_sound_latch_registers(m,g,v,w,e,c);}
int main(void){
    test_part_shapes_bind();test_parts_intact();test_formation_slot();test_explosion_entry();test_flat_draw();
    test_projectile_draw();test_beam_done();test_weapon_by_mount();test_ptr_list_add();test_box_square_init();
    test_latch(twist,0x49a1c9u,0x49a1cau,0x298u);test_latch(pitch,0x49a1ccu,0x49a1ceu,0x29au);
    test_type_index();test_notice_time();test_notices();
    printf("sim_leaves_test: %u checks passed\n",checks);return 0;
}
