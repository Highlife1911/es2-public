#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/ai_order.h"
/* Synthetic guest for the AI order helpers. Objects live in an
 * arena at 90000000h, the globals and script tables in a window of an
 * image at B. Bridged callees are recording stand-ins: memset 49681A
 * really clears and returns (dst, 0x5e00+size) with ECX = 0x5eedNNNN;
 * 41D5EC returns 0xd5ec/0xd5ed; 413E50 and 41C0F4 run the core again
 * (like replaced hooks); 41EB34 calls the core recursively. Expected
 * values are worked out by hand from docs/reverse-engineering/mech_order.md. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[0x4000],globals[0x20000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"ai_order line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x490000)<sizeof(globals))return globals+(a-G(0x490000));
    fprintf(stderr,"ai_order: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
enum { GRP=0x100, ORD=0x200, CFG=0x280, TBL=0x300, ACT=0x800 };
#define AT(o) (OBJ+(uint32_t)(o))
#define ACTOR(i) AT(ACT+0x300*(i))
#define VEH ACTOR(0)
static const es2_weapon_graph *graph;
static unsigned memsets,ai_calls,scripts,attacks,resumes;
static uint32_t memset_regs[2][3],last_table,last_regs[3];
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;(void)n;uint32_t entry[3]={r->eax,r->edx,r->ecx};uint64_t p=0;
    if(f!=G(0x49681a))memcpy(last_regs,entry,sizeof(last_regs));
    if(f==G(0x49681a)){
        if(memsets<2)memcpy(memset_regs[memsets],entry,sizeof(entry));
        ++memsets;for(uint32_t i=0;i<a[2];++i)w8(a[0]+i,(uint8_t)a[1]);
        r->eax=a[0];r->edx=0x5e00+a[2];r->ecx=0x5eed0000u+memsets;return;
    }
    if(f==G(0x41d5ec)){++ai_calls;CHECK(a[0]==VEH);p=pair(0xd5ec,0xd5ed);}
    else if(f==G(0x413e50)){++scripts;last_table=a[1];p=es2_script_start_registers(&memory,graph,a[0],a[1]);}
    else if(f==G(0x41c0f4)){++attacks;p=es2_attack_target_registers(&memory,graph,a[0],a[1]);}
    else if(f==G(0x41eb34)){++resumes;p=es2_order_resume_registers(&memory,graph,a[0],entry);}
    else {fprintf(stderr,"ai_order: unexpected call %08x\n",f);exit(1);}
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);r->ecx=0x5eedffffu;
}
static void reset(void){
    memset(arena,0,sizeof(arena));memset(globals,0,sizeof(globals));
    memsets=ai_calls=scripts=attacks=resumes=0;last_table=0;
    for(unsigned i=0;i<6;++i){uint32_t A=ACTOR(i);w32(0,A+0x45,AT(GRP));w32(0,A+0x1f2,AT(CFG));}
    /* Group: leader slot +0Ch -> A1, the player; order slots empty. */
    w32(0,AT(GRP+0xc),AT(GRP+0x40));w32(0,AT(GRP+0x40),ACTOR(1));w32(0,G(0x4a9b84),ACTOR(1));
    /* Script tables: initial timer +4 = 100 + low address byte. */
    static const uint32_t tables[]={0x499420,0x4993e2,0x499518,0x499556,0x499594,0x49968c,0x4997c2,0x499746,0x4996ca,0x49964e,0x4995d2,0x499610};
    for(unsigned i=0;i<sizeof(tables)/sizeof(tables[0]);++i)w32(0,G(tables[i])+4,100+(tables[i]&0xff));
}
static void class_checks(void){
    reset();const uint32_t entry[3]={1,2,3},T=ACTOR(2);
    CHECK(es2_target_class_registers(&memory,T,entry)==pair(0,2));
    w32(0,T+0x4d,AT(TBL));CHECK(es2_target_class_registers(&memory,T,entry)==pair(0,AT(TBL+8)));
    w8(AT(TBL+0xc),1);CHECK(es2_target_class_registers(&memory,T,entry)==pair((T&0xffff0000u)|1u,AT(TBL+8)));
    w8(AT(TBL+0xd),1);CHECK(es2_target_class_registers(&memory,T,entry)==pair((T&0xffff0000u)|2u,AT(TBL+8)));
    w8(AT(TBL+0xc),0);CHECK(es2_target_class_registers(&memory,T,entry)==pair((T&0xffff0000u)|2u,AT(TBL+8)));
}
static void script_checks(void){
    reset();const uint32_t S=VEH+0x4d;
    memset(arena+ACT+0x4d,0xee,0x50);w16(0,G(0x4a9b70),0xfff7);
    /* FFF7h + 13 = 4h: timer = 100 + 94h + 4. */
    CHECK(es2_script_start_registers(&memory,graph,S,G(0x499594))==pair(S+0xd,0x5e28));
    CHECK(r32(0,S)==G(0x499594)&&r16(0,G(0x4a9b70))==4&&r32(0,S+5)==100+0x94+4&&r32(0,S+9)==0);
    CHECK(r8(S+0x36)==1&&r8(S+0x35)==0&&r8(S+0x44)==0&&r8(S+0xd)==0&&r8(S+0x34)==0&&r8(S+0x45)==0xee);
    CHECK(memsets==2&&memset_regs[0][0]==S+0x35&&memset_regs[0][1]==4&&memset_regs[0][2]==4);
    CHECK(memset_regs[1][0]==S+0x35&&memset_regs[1][1]==0x5e10&&memset_regs[1][2]==0x5eed0001u);
    /* Low bits 0Fh: 2 + 13. */
    w16(0,G(0x4a9b70),2);es2_script_start_registers(&memory,graph,S,G(0x499594));CHECK(r32(0,S+5)==100+0x94+15);
}
static void attack_checks(void){
    /* A0 attacks A2 (type 1) after A3: counts move, B+0Bh set, script 499518h. */
    reset();const uint32_t A=VEH;w32(0,A+0x1a4,ACTOR(3));w16(0,ACTOR(3)+0x1a2,5);w16(0,ACTOR(2)+0x1a2,1);w16(0,ACTOR(2)+0x1a8,1);
    CHECK(es2_attack_target_registers(&memory,graph,A,ACTOR(2))==pair(A+0x4d+0xd,0x5e28));
    CHECK(r32(0,A+0x1a4)==ACTOR(2)&&r16(0,ACTOR(3)+0x1a2)==4&&r16(0,ACTOR(2)+0x1a2)==2&&r8(A+0x9d)==1&&last_table==G(0x499518));
    CHECK(last_regs[0]==A+0x4d&&last_regs[1]==0&&last_regs[2]==ACTOR(2));
    /* The same target again: no count change; type 2 and 3. */
    w8(A+0x9d,0);w16(0,ACTOR(2)+0x1a8,2);es2_attack_target_registers(&memory,graph,A,ACTOR(2));
    CHECK(r16(0,ACTOR(2)+0x1a2)==2&&r8(A+0x9d)==0&&last_table==G(0x499556));
    w16(0,ACTOR(2)+0x1a8,3);es2_attack_target_registers(&memory,graph,A,ACTOR(2));CHECK(last_table==G(0x499518)&&scripts==3);
    /* Type 0: 41D5EC with EDX = B, B+1Ah set. */
    w16(0,ACTOR(2)+0x1a8,0);CHECK(es2_attack_target_registers(&memory,graph,A,ACTOR(2))==pair(0xd5ec,0xd5ed));
    CHECK(ai_calls==1&&r8(A+0xac)==1&&last_regs[0]==A&&last_regs[1]==A+0x92&&last_regs[2]==ACTOR(2));
    /* Other types: EAX = A, EDX = v - 3; no old target. */
    reset();w16(0,ACTOR(2)+0x1a8,7);CHECK(es2_attack_target_registers(&memory,graph,A,ACTOR(2))==pair(A,4));
    w16(0,ACTOR(2)+0x1a8,0xffff);CHECK(es2_attack_target_registers(&memory,graph,A,ACTOR(2))==pair(A,0xfffffffcu));
    CHECK(scripts==0&&r16(0,ACTOR(2)+0x1a2)==1);
}
static uint64_t resume(uint32_t edx){const uint32_t entry[3]={0x1111,edx,0x3333};return es2_order_resume_registers(&memory,graph,VEH,entry);}
static void resume_checks(void){
    const uint32_t A=VEH;
    /* Own flag B+11h: table by the config word +50h; target dropped. */
    reset();w8(A+0xa3,1);w32(0,A+0x1a4,ACTOR(3));w16(0,ACTOR(3)+0x1a2,2);
    CHECK(resume(0)==pair(((A+0x92)&0xffff0000u)|1u,0)&&last_table==G(0x4993e2)&&r32(0,A+0x1a4)==0&&r16(0,ACTOR(3)+0x1a2)==1&&r8(A+0x9d)==1);
    w16(0,AT(CFG+0x50),1);CHECK(resume(0)==pair(((A+0x4d+0xd)&0xffff0000u)|1u,0)&&last_table==G(0x499420));
    /* The player leads: state 1 -> 499594h; 3 -> nothing (EAX = leader). */
    reset();w16(0,A+0x23e,1);CHECK(resume(0)==pair(((A+0x5a)&0xffff0000u)|1u,0)&&last_table==G(0x499594)&&scripts==1);
    w16(0,A+0x23e,3);CHECK(resume(0)==pair((ACTOR(1)&0xffff0000u)|1u,3)&&scripts==1);
    w16(0,A+0x23e,0x8000);CHECK(resume(0)==pair((ACTOR(1)&0xffff0000u)|1u,0xffff8000u)&&scripts==1);
    /* State 6 -> 499746h with ECX = the leader slot. */
    w16(0,A+0x23e,6);resume(0);CHECK(last_table==G(0x499746)&&scripts==2);
    /* State 4: the order target alive -> 41C0F4 (type 9: EAX = A, EDX = 6);
     * dead -> state 0 and itself (then the group path). */
    reset();w16(0,A+0x23e,4);w32(0,A+0x248,ACTOR(4));w16(0,ACTOR(4)+0x1a8,9);
    CHECK(resume(0)==pair((A&0xffff0000u)|1u,6)&&attacks==1&&r32(0,A+0x1a4)==ACTOR(4));
    w8(ACTOR(4)+0x99,1);w32(0,AT(GRP+0x44),AT(ORD));w16(0,AT(ORD),3);
    CHECK(resume(0)==pair(((A+0x92)&0xffff0000u)|1u,0)&&resumes==1&&r16(0,A+0x23e)==0&&last_table==G(0x499594)&&r32(0,A+0x1a4)==0);
    /* Another leader: the group's order type picks the table. */
    reset();w32(0,G(0x4a9b84),ACTOR(5));w32(0,AT(GRP+0x6c),1);w32(0,AT(GRP+0x48),AT(ORD));
    static const uint32_t types[]={0x49968c,0x4997c2,0x499746,0x499594,0x4996ca,0x4995d2,0x499610};
    for(uint16_t t=0;t<7;++t){w16(0,AT(ORD),t);resume(0);CHECK(last_table==G(types[t]));}
    w16(0,AT(ORD),5);w16(0,AT(CFG+0x22),0x7d00);resume(0);CHECK(last_table==G(0x4995d2));
    w16(0,AT(CFG+0x22),0x7d01);resume(0);CHECK(last_table==G(0x49964e));
    w16(0,AT(CFG+0x22),0x8000);resume(0);CHECK(last_table==G(0x4995d2));
    /* Empty slot (0Bh) or type 7: the table is the entry EDX. */
    w16(0,AT(ORD),7);resume(G(0x499610));CHECK(last_table==G(0x499610)&&last_regs[1]==G(0x499610)&&last_regs[2]==A+0x4d);
    w32(0,AT(GRP+0x48),0);resume(G(0x499746));CHECK(last_table==G(0x499746));
}
int main(void){
    es2_weapon_graph g={B,0,0,0,0,call,NULL,NULL,NULL};graph=&g;
    class_checks();script_checks();attack_checks();resume_checks();
    printf("ai_order core: %u checks passed\n",checks);
    return 0;
}
