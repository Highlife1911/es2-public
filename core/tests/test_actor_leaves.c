#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/actor_leaves.h"
/* Synthetic guest: an arena at 90000000h (object O at +100h, configuration
 * C at +600h, part tables, mount record, out word) and a synthetic image
 * base I holding the MECH slot table 0x49a060 (the reference's seven words
 * 0, 4, 5, 7, 8, 9, 10). Game calls are logged and answered by a stub.
 * Expectations from the reference listing (vtables.md, section "actor slot
 * leaves"). */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define O (ARENA+0x100)
#define C (ARENA+0x600)
#define TABLE (ARENA+0x800)
#define WORDS (ARENA+0x900)
#define MOUNT (ARENA+0xa00)
#define OUT (ARENA+0xb00)
#define BRECORD (ARENA+0xc00)
static uint8_t arena[0x1000],slots[16];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"actor_leaves line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    if(a-(I+0x9a060u)<sizeof(slots))return slots+(a-(I+0x9a060u));
    fprintf(stderr,"actor_leaves: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static const es2_guest_memory *m=&memory;
static uint32_t call_target,call_args[2],call_regs[3];static unsigned calls,observed;
static void stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;++calls;call_target=target;memcpy(call_args,a,4*n);call_regs[0]=r->eax;call_regs[1]=r->edx;call_regs[2]=r->ecx;
    r->eax=0x11112222u;r->edx=0x33334444u;r->ecx=0x55556666u;
}
static uint32_t observed_function;
static void observe(void *context,uint32_t function,unsigned slot){(void)context;CHECK(slot==0);observed_function=function;++observed;}
static const uint32_t entry[3]={0xabcd1234u,0x5678ef01u,0x0badf00du};
#define EAX(p) ((uint32_t)(p))
#define EDX(p) ((uint32_t)((p)>>32))
static void constants(void){
    uint64_t (*const zero[])(const uint32_t *,uint32_t *)={es2_base_decide_registers,es2_actor_armour_registers,
        es2_actor_target_scale_registers,es2_actor_angle_registers,es2_actor_order_registers,es2_actor_keys_registers,
        es2_actor_aim_node_registers,es2_actor_radius_registers,es2_actor_sight_radius_registers,es2_object_zero_registers,
        es2_flyer_keys_registers};
    uint64_t (*const one[])(const uint32_t *,uint32_t *)={es2_base_in_front_registers,es2_actor_move_registers,
        es2_actor_engage_roll_registers,es2_actor_mount_word_registers};
    uint32_t ecx=0;
    for(unsigned i=0;i<sizeof(zero)/sizeof(zero[0]);++i){const uint64_t p=zero[i](entry,&ecx);
        CHECK(EAX(p)==0&&EDX(p)==entry[1]&&ecx==entry[2]);}
    for(unsigned i=0;i<sizeof(one)/sizeof(one[0]);++i){const uint64_t p=one[i](entry,&ecx);
        CHECK(EAX(p)==0xabcd0001u&&EDX(p)==entry[1]&&ecx==entry[2]);}
    uint64_t p=es2_actor_part_word_registers(entry,&ecx);CHECK(EAX(p)==1&&EDX(p)==entry[1]&&ecx==entry[2]);
    p=es2_actor_empty_registers(entry,&ecx);CHECK(EAX(p)==entry[0]&&EDX(p)==entry[1]&&ecx==entry[2]);
    p=es2_actor_no_part_registers(entry,&ecx);CHECK(EAX(p)==0xffffffffu&&EDX(p)==entry[1]&&ecx==entry[2]);
}
static void getters(void){
    uint32_t ecx=0;
    w32(0,O+0x1f2,C);w16(0,O+0x20f,0x8123);w16(0,O+0x298,0x7abc);w16(0,C+0x2a,0xfedc);w16(0,C+0x70,0x0042);
    uint64_t p=es2_gun_angle_registers(m,O,entry,&ecx);CHECK(EAX(p)==((O&0xffff0000u)|0x8123u)&&EDX(p)==entry[1]&&ecx==entry[2]);
    p=es2_mech_angle_registers(m,O,entry,&ecx);CHECK(EAX(p)==((O&0xffff0000u)|0x7abcu)&&EDX(p)==entry[1]);
    p=es2_base_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==((C&0xffff0000u)|0xfedcu)&&EDX(p)==entry[1]&&ecx==entry[2]);
    p=es2_mech_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==((C&0xffff0000u)|0x42u));
    p=es2_mech_sight_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==((C&0xffff0000u)|0x42u)&&EDX(p)==entry[1]);
    /* actor_in_front: a - [O+10h] + 3800h below 7000h. */
    w16(0,O+0x10,0x1000);
    p=es2_actor_in_front_registers(m,O,0xffff1000u,entry,&ecx);CHECK(EAX(p)==1&&EDX(p)==0x56783800u&&ecx==entry[2]);
    p=es2_actor_in_front_registers(m,O,0x47ffu,entry,&ecx);CHECK(EAX(p)==1&&EDX(p)==0x56786fffu);   /* 6FFFh */
    p=es2_actor_in_front_registers(m,O,0x4800u,entry,&ecx);CHECK(EAX(p)==0&&EDX(p)==0x56787000u);   /* 7000h */
    p=es2_actor_in_front_registers(m,O,0xd7ffu,entry,&ecx);CHECK(EAX(p)==0&&EDX(p)==0x5678ffffu);   /* FFFFh */
    p=es2_actor_in_front_registers(m,O,0xd800u,entry,&ecx);CHECK(EAX(p)==1&&EDX(p)==0x56780000u);   /* wraps to 0 */
    /* actor_mark */
    arena[0x100+0xb1]=0;p=es2_actor_mark_registers(m,O,entry,&ecx);
    CHECK(arena[0x100+0xb1]==1&&EAX(p)==O+0x92u&&EDX(p)==entry[1]&&ecx==entry[2]);
}
static void sight_radius(void){
    uint32_t ecx=0;uint64_t p;
    w32(0,O+0x1f2,C);w16(0,C+0x2a,0x1357);w32(0,O+0x205,BRECORD);
    w16(0,C+6,0);p=es2_base_sight_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==0&&EDX(p)==C&&ecx==entry[2]);
    w16(0,C+6,0x8000);w16(0,C+4,0xffff);w16(0,BRECORD+5,0);arena[0x199]=1;
    p=es2_base_sight_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==((O&0xffff0000u)|0x1357u)&&EDX(p)==0&&ecx==C);
    w16(0,C+4,0xfffe);w16(0,BRECORD+5,2);
    p=es2_base_sight_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==((O&0xffff0000u)|0x1357u)&&EDX(p)==0&&ecx==C);
    w16(0,BRECORD+5,0xffff);arena[0x199]=0;
    p=es2_base_sight_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==((O&0xffff0000u)|0x1357u)&&EDX(p)==0&&ecx==C);
    arena[0x199]=0x80;
    p=es2_base_sight_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==0&&EDX(p)==1&&ecx==C);
    w16(0,BRECORD+5,0x7fff);   /* 32767 >= 2 */
    p=es2_base_sight_radius_registers(m,O,entry,&ecx);CHECK(EAX(p)==((O&0xffff0000u)|0x1357u)&&EDX(p)==0);
}
static void part_words(void){
    uint32_t ecx=0;uint64_t p;
    w32(0,O+0x201,TABLE);w32(0,O+0x20e,TABLE);
    for(unsigned i=0;i<16;++i)w16(0,TABLE+2*i,(uint16_t)(i%3==0?0:0x8000u+i));
    p=es2_base_part_word_registers(m,O,1,entry,&ecx);CHECK(EAX(p)==0xffff8001u&&EDX(p)==1&&ecx==0xffff8001u);
    p=es2_base_part_word_registers(m,O,0xffffffffu,entry,&ecx);CHECK(EDX(p)==0xffffffffu&&EAX(p)==(uint32_t)(int16_t)r16(0,TABLE-2));
    p=es2_mech_part_word_registers(m,O,2,entry,&ecx);CHECK(EAX(p)==0xffff8002u&&EDX(p)==2&&ecx==entry[2]);
    w32(0,O+0x202,MOUNT);   /* overlaps +201h */
    w16(0,MOUNT+0x0a+2*3,0x4444);w16(0,MOUNT+0x0a-2,0x5555);
    p=es2_mech_mount_word_registers(m,O,0xabcd0003u,entry,&ecx);CHECK(EAX(p)==((MOUNT&0xffff0000u)|0x4444u)&&EDX(p)==3&&ecx==entry[2]);
    p=es2_mech_mount_word_registers(m,O,0x0000ffffu,entry,&ecx);CHECK(EAX(p)==((MOUNT&0xffff0000u)|0x5555u)&&EDX(p)==0xffffffffu);
}
static void next_parts(void){
    uint32_t ecx=0;uint64_t p;
    /* BASE: four parts, words 0, 5, 0, 0. */
    w32(0,O+0x1f2,C);w32(0,O+0x201,TABLE);w16(0,C+0x12,4);
    static const uint16_t base_words[]={0,5,0,0};
    for(unsigned i=0;i<4;++i)w16(0,TABLE+2*i,base_words[i]);
    w32(0,OUT,0x12345678u);p=es2_base_next_part_registers(m,O,1,OUT,entry,&ecx);   /* 2,3,0 empty, back to 1 */
    CHECK(EAX(p)==0xffffffffu&&r32(0,OUT)==0xffffffffu&&EDX(p)==OUT&&ecx==1);
    p=es2_base_next_part_registers(m,O,2,OUT,entry,&ecx);CHECK(EAX(p)==1&&r32(0,OUT)==1&&ecx==2);
    p=es2_base_next_part_registers(m,O,0xffff0000u,OUT,entry,&ecx);CHECK(EAX(p)==1&&ecx==0);   /* negative: from 0 */
    w16(0,C+0x12,1);w16(0,TABLE,0);
    p=es2_base_next_part_registers(m,O,0,OUT,entry,&ecx);CHECK(EAX(p)==0xffffffffu);   /* one part: 1 wraps to 0 = start */
    w16(0,C+0x12,4);w16(0,TABLE+2*3,9);
    p=es2_base_next_part_registers(m,O,3,OUT,entry,&ecx);CHECK(EAX(p)==1);              /* 3 -> 0 (empty) -> 1 */
    /* MECH: slot table 0,4,5,7,8,9,10; words at TABLE + 2*slot. */
    static const uint16_t t[]={0,4,5,7,8,9,10,0};
    memcpy(slots,t,sizeof(t));w32(0,O+0x20e,WORDS);
    for(unsigned i=0;i<12;++i)w16(0,WORDS+2*i,0);
    w16(0,WORDS+2*7,1);
    p=es2_mech_next_part_registers(m,I,O,0,OUT,entry,&ecx);CHECK(EAX(p)==3&&EDX(p)==7&&r32(0,OUT)==7&&ecx==OUT);
    p=es2_mech_next_part_registers(m,I,O,3,OUT,entry,&ecx);CHECK(EAX(p)==0xffffffffu&&EDX(p)==0xffffffffu&&r32(0,OUT)==0xffffffffu);
    w16(0,WORDS,2);
    p=es2_mech_next_part_registers(m,I,O,6,OUT,entry,&ecx);CHECK(EAX(p)==0&&EDX(p)==0);    /* 6 wraps to 0 */
    p=es2_mech_next_part_registers(m,I,O,0x80000000u,OUT,entry,&ecx);CHECK(EAX(p)==3);     /* negative: from 0 */
    w32(0,OUT,0);p=es2_actor_next_part_registers(m,OUT,entry,&ecx);
    CHECK(EAX(p)==0xffffffffu&&EDX(p)==0xffffffffu&&r32(0,OUT)==0xffffffffu&&ecx==entry[2]);
}
static void game_calls(void){
    es2_weapon_graph g;memset(&g,0,sizeof(g));g.image=I;g.call=stub;g.observe=observe;
    uint32_t ecx=0;uint64_t p;
    calls=0;observed=0;p=es2_mech_condition_registers(m,&g,O,entry,&ecx);
    CHECK(calls==1&&observed==1&&observed_function==I+0x0db2cu&&call_target==I+0x0db2cu&&call_args[0]==O+0x206u&&call_regs[0]==O+0x206u&&
          call_regs[1]==entry[1]&&call_regs[2]==entry[2]&&EAX(p)==0x11112222u&&EDX(p)==0x33334444u&&ecx==O+0x206u);
    p=es2_flyer_condition_registers(m,&g,O,entry,&ecx);
    CHECK(calls==2&&call_args[0]==O+0x200u&&call_regs[0]==O+0x200u&&ecx==O+0x200u&&EAX(p)==0x11112222u);
    w16(0,O+0x233,0xfff0);
    p=es2_flyer_target_scale_registers(m,&g,O,entry,&ecx);
    CHECK(calls==3&&call_target==I+0x7e0b0u&&call_args[0]==1000&&call_args[1]==0xfffffff0u&&
          memcmp(call_regs,entry,12)==0&&EAX(p)==0x11112222u&&EDX(p)==0x33334444u&&ecx==0x55556666u);
}
/* actor_radial_hit: three game calls in order (slot +5Ch, dist_gate, slot
 * +74h), each answered from a plan; the log keeps target, slot, arguments and
 * the entry registers. */
#define VT (ARENA+0xd00)
static struct { uint32_t target[3],slot[3],args[3][4],regs[3][3];unsigned n[3];unsigned count;
    uint32_t radius_eax,dist,tail_eax,tail_edx,tail_ecx,swap; } rh;
static void rh_observe(void *context,uint32_t function,unsigned slot){
    (void)context;CHECK(rh.count<3);rh.target[rh.count]=function;rh.slot[rh.count]=slot;
}
static void rh_stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;const unsigned k=rh.count++;CHECK(k<3&&target==rh.target[k]);
    rh.n[k]=n;memcpy(rh.args[k],a,4*n);rh.regs[k][0]=r->eax;rh.regs[k][1]=r->edx;rh.regs[k][2]=r->ecx;
    if(k==0){r->eax=rh.radius_eax;r->edx=0x1111u;r->ecx=0x2222u;}
    else if(k==1){if(rh.swap)w32(0,O,rh.swap);r->eax=rh.dist;r->edx=0x3333u;r->ecx=0x4444u;}
    else{r->eax=rh.tail_eax;r->edx=rh.tail_edx;r->ecx=rh.tail_ecx;}
}
static void radial_hit(void){
    es2_weapon_graph g;memset(&g,0,sizeof(g));g.image=I;g.call=rh_stub;g.observe=rh_observe;
    const uint32_t point=ARENA+0xe00,source=0x12340000u|ARENA;uint32_t ecx=0;uint64_t p;
    w32(0,O,VT);w32(0,VT+0x5c,0x22225c00u);w32(0,VT+0x74,0x22227400u);
    struct { uint32_t radius,dist,damage,reach,q,rem; } plan[]={
        {0x1234fff8u,100,0xffff0064u,0x00010100u,57,208},        /* 148*100/256 */
        {0x00000000u,10,0xfffffffdu,0x0000ffceu,0xfffffffdu,30}, /* -60*-3/-50 */
        {0xabcd0000u,350,7,100,0xffffffefu,0xffffffceu},         /* -250*7/100 = -17 rem -50 */
        {0x00007fffu,0,0x8000,0x7fff,0,0},                       /* radius 32767; (32767-(0-32767))*-32768/32767 */
    };
    /* The last row: 65534 * -32768 = -2147418112 (fits); / 32767 = -65536 rem 0. */
    plan[3].q=0xffff0000u;plan[3].rem=0;
    for(unsigned i=0;i<sizeof(plan)/sizeof(plan[0]);++i){
        memset(&rh,0,sizeof(rh));rh.radius_eax=plan[i].radius;rh.dist=plan[i].dist;
        rh.tail_eax=0xaaaa0001u;rh.tail_edx=0xbbbb0002u;rh.tail_ecx=0x77u;
        p=es2_actor_radial_hit_registers(m,&g,O,plan[i].damage,point,plan[i].reach,source,entry,&ecx);
        CHECK(rh.count==3);
        CHECK(rh.target[0]==0x22225c00u&&rh.slot[0]==0x5c&&rh.n[0]==1&&rh.args[0][0]==O&&
              rh.regs[0][0]==VT&&rh.regs[0][1]==entry[1]&&rh.regs[0][2]==entry[2]);
        CHECK(rh.target[1]==I+0x9288cu&&rh.slot[1]==0&&rh.n[1]==2&&rh.args[1][0]==point&&rh.args[1][1]==O+0x26u&&
              rh.regs[1][0]==point&&rh.regs[1][1]==(uint32_t)(int32_t)(int16_t)plan[i].radius&&rh.regs[1][2]==O+0x26u);
        CHECK(rh.target[2]==0x22227400u&&rh.slot[2]==0x74&&rh.n[2]==4);
        if(rh.args[2][0]!=O||rh.args[2][1]!=0||rh.args[2][2]!=plan[i].q||rh.args[2][3]!=source)fprintf(stderr,"row %u args %08x %08x %08x %08x want q %08x source %08x\n",i,rh.args[2][0],rh.args[2][1],rh.args[2][2],rh.args[2][3],plan[i].q,source);
        CHECK(rh.args[2][0]==O&&rh.args[2][1]==0&&rh.args[2][2]==plan[i].q&&rh.args[2][3]==source);
        if(rh.regs[2][1]!=plan[i].rem)fprintf(stderr,"row %u q %08x rem %08x want %08x\n",i,rh.args[2][2],rh.regs[2][1],plan[i].rem);
        CHECK(rh.regs[2][0]==VT&&rh.regs[2][1]==plan[i].rem&&rh.regs[2][2]==source);
        CHECK(EAX(p)==0xaaaa0001u&&EDX(p)==0xbbbb0002u&&ecx==0x77u);
    }
    /* The vtable is read again before the tail call: dist_gate replaces it. */
    memset(&rh,0,sizeof(rh));rh.radius_eax=0;rh.dist=5;rh.swap=VT+0x100;
    w32(0,VT+0x100+0x74,0x33337400u);
    p=es2_actor_radial_hit_registers(m,&g,O,10,point,100,source,entry,&ecx);
    CHECK(rh.target[0]==0x22225c00u&&rh.target[2]==0x33337400u&&rh.regs[2][0]==VT+0x100);
}
int main(void){
    constants();getters();sight_radius();part_words();next_parts();game_calls();radial_hit();
    printf("PASS: actor_leaves %u checks\n",checks);
    return 0;
}
