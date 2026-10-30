#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/building.h"
/* Synthetic state for the cores (turret_target 403eec, part_facing
 * 406868). The image base is placed so that the guard byte 4974d5, the
 * angle triple 4a95ec and the default record 6c56a8 fall inside the arena.
 * The callees are small models that log their entry registers; the
 * virtual slots are two fixed addresses. All values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0x97000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJECTS (BASE+0x2d0000u)
#define FRAME (BASE+0x2e0000u)
#define SLOT20 UINT32_C(0x12345678)
#define SLOT58 UINT32_C(0x23456789)
static uint8_t arena[0x2e1000];static unsigned checks,ncalls;
static uint32_t calls[32],regs[32][3],slot_result,angle_words[3];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"building_aim line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<32);calls[ncalls]=f;regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;++ncalls;
    uint32_t eax=0x1000u+ncalls,edx=0x2000u+ncalls;
    if(f==SLOT20){CHECK(n==2&&a[1]==2);eax=slot_result;}
    else if(f==VA(0x47df19)){CHECK(n==1);eax=a[0];}
    else if(f==VA(0x48043c)||f==VA(0x480304)){CHECK(n==3);for(unsigned j=0;j<3;++j)w32(0,a[2]+4*j,r32(0,a[0]+4*j)+j);}
    else if(f==VA(0x492990)){CHECK(n==3);for(unsigned j=0;j<3;++j)w16(0,a[0]+2*j,(uint16_t)angle_words[j]);eax=a[0];}
    else if(f==SLOT58){CHECK(n==3);for(unsigned j=0;j<3;++j)w32(0,a[2]+4*j,a[1]);}
    else if(f==VA(0x47ebb8)||f==VA(0x403d5c)){CHECK(n==2);}
    else CHECK(0);
    r->eax=eax;r->edx=edx;r->ecx=0x3000u+ncalls;
}
static es2_weapon_graph graph(void){es2_weapon_graph g={IMAGE,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
int main(void){
    es2_weapon_graph g=graph();
    uint32_t b=OBJECTS,mgr=OBJECTS+0x400,shape=OBJECTS+0x440,vt=OBJECTS+0x460,res=OBJECTS+0x4a0,rec=OBJECTS+0x500,p=OBJECTS+0x700;
    /* turret_target: guard set, matrix built, record 2 selected. */
    w32(0,b+0x34,mgr);w32(0,mgr+4,shape);w32(0,mgr+0x16,rec);w32(0,shape,vt);w32(0,vt+0x20,SLOT20);
    w16(0,res+4,2);slot_result=res;
    w32(0,b+0x26,100);w32(0,b+0x2a,0xfffffff6u);w32(0,b+0x2e,7);
    w32(0,rec+64+0x14,5);w32(0,rec+64+0x18,6);w32(0,rec+64+0x1c,0x80000000u);
    uint64_t v=es2_turret_target_registers(&memory,&g,b,p,entry);
    CHECK((uint32_t)v==VA(0x4a95ec)&&(uint32_t)(v>>32)==0x2000u+ncalls);
    CHECK(*(uint8_t *)host(0,VA(0x4974d5))==1&&r16(0,b+0x32)==1);
    CHECK(ncalls==10&&calls[0]==VA(0x47ebb8)&&calls[1]==SLOT20&&calls[2]==VA(0x47df19)&&calls[3]==VA(0x480304));
    CHECK(calls[6]==VA(0x48043c)&&calls[7]==VA(0x48043c)&&calls[8]==VA(0x492990)&&calls[9]==VA(0x403d5c));
    CHECK(regs[0][0]==b+0x12&&regs[0][1]==b+0xc&&regs[0][2]==0);
    CHECK(regs[1][0]==shape&&regs[1][1]==vt&&regs[1][2]==b);
    /* record 2's translation reaches the first transpose's registers. */
    CHECK(regs[2][0]==0x80000000u&&regs[2][1]==5&&regs[2][2]==6);
    /* Negated building translation: T1 = (-100, 10, -7); ECX = -7. */
    CHECK(regs[3][0]==FRAME+0x38+0x14&&regs[3][1]==FRAME+0xc&&regs[3][2]==(uint32_t)-7);
    CHECK(r32(0,FRAME+0xc)==(uint32_t)-100&&r32(0,FRAME+0x10)==10);
    CHECK(regs[5][2]==0x80000000u);   /* -INT_MIN wraps */
    CHECK(regs[6][0]==p&&regs[6][1]==FRAME+0x64&&regs[7][0]==FRAME+0x64&&regs[7][2]==FRAME+0x70);
    CHECK(regs[8][1]==FRAME+0x58&&regs[8][2]==FRAME+0x70&&regs[9][0]==b);
    /* Null result and negative index both select the default record. */
    *(uint8_t *)host(0,VA(0x4974d5))=7;ncalls=0;slot_result=0;w32(0,VA(0x6c56a8)+0x14,9);
    es2_turret_target_registers(&memory,&g,b,p,entry);
    CHECK(*(uint8_t *)host(0,VA(0x4974d5))==7&&calls[0]==SLOT20&&regs[1][1]==9);
    ncalls=0;slot_result=res;w16(0,res+4,0xffff);es2_turret_target_registers(&memory,&g,b,p,entry);
    CHECK(regs[1][1]==9);
    /* part_facing: definition with 4 parts, flags 0,1,1,1. */
    uint32_t def=OBJECTS+0x800,flags=OBJECTS+0x840,bvt=OBJECTS+0x880;
    w32(0,b,bvt);w32(0,bvt+0x58,SLOT58);w32(0,b+0x1f2,def);w32(0,b+0x201,flags);w16(0,def+0x12,4);
    w16(0,flags,0);w16(0,flags+2,1);w16(0,flags+4,1);w16(0,flags+6,1);
    w16(0,p,0x100);w16(0,p+2,0);w16(0,p+4,0x200);
    /* No viewer: the first living part, EAX all 32 bits, EDX = B. */
    ncalls=0;v=es2_part_facing_registers(&memory,&g,b,0,entry);
    CHECK(v==((uint64_t)b<<32|1u)&&ncalls==0);
    /* Every part sees the same angles: the first measured part wins ties. */
    angle_words[0]=0x100+5;angle_words[1]=0;angle_words[2]=0x200-7;ncalls=0;
    v=es2_part_facing_registers(&memory,&g,b,p,entry);
    CHECK(v==((uint64_t)b<<32|1u)&&ncalls==6);
    CHECK(calls[0]==SLOT58&&regs[0][0]==FRAME&&regs[0][1]==b&&regs[0][2]==bvt);
    CHECK(regs[1][0]==p+0x1a&&regs[1][1]==FRAME&&regs[1][2]==FRAME+0xc);
    /* 8000h saturates to 7FFFh: 7FFFh + 0 still beats 1000000h. */
    angle_words[0]=0x100+0x8000;angle_words[2]=0x200;w16(0,def+0x12,2);ncalls=0;
    CHECK(es2_part_facing_registers(&memory,&g,b,p,entry)==((uint64_t)b<<32|1u));
    /* No living part: AX = FFFFh over the entry EAX high word. */
    w16(0,flags+2,0);ncalls=0;
    CHECK(es2_part_facing_registers(&memory,&g,b,p,entry)==((uint64_t)b<<32|0x1111ffffu));
    w16(0,def+0x12,0xffff);
    CHECK(es2_part_facing_registers(&memory,&g,b,0,entry)==((uint64_t)b<<32|0x1111ffffu));
    printf("building_aim_test: %u checks passed\n",checks);
    return 0;
}
