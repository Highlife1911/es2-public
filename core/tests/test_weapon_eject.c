#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/weapon_eject.h"
/* Synthetic guest: the weapon, its definition, records, the vehicle with
 * its vtable, shape, node tree and aim node, the debris object and shape
 * table at 90000000h; an image base I with the globals 0x4a9642,
 * 0x4a9868, 0x4a98f4 and 0x6cbee4; a frame scratch at A0000000h. Every
 * game call is logged with its arguments and entry registers; the stubs
 * return planned values and write the output buffers. The expectations are
 * read from the reference listing (weapons.md, section "weapon
 * eject"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_WEAPON_EJECT_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_WEAPON_EJECT_SCRATCH],globals1[0x400],globals2[0x10];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"weapon_eject line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4a9600)<sizeof(globals1))return globals1+(a-G(0x4a9600));
    if(a-G(0x6cbee0)<sizeof(globals2))return globals2+(a-G(0x6cbee0));
    fprintf(stderr,"weapon_eject: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define W (OBJ+0x100)
#define D (OBJ+0x200)
#define C1C (OBJ+0x240)
#define TYPE (OBJ+0x280)
#define A (OBJ+0x300)
#define VTA (OBJ+0x3c0)
#define S (OBJ+0x400)
#define R (OBJ+0x440)
#define VTR (OBJ+0x460)
#define N (OBJ+0x480)
#define NODES (OBJ+0x500)
#define O (OBJ+0x600)
#define TABLE (OBJ+0x700)
#define COUNTER (OBJ+0x780)
#define AIM (OBJ+0x7a0)
#define MOUNT (OBJ+0x7c0)
#define S20 UINT32_C(0x50000020)
#define S24 UINT32_C(0x50000024)
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; uint16_t context; } logged;
static logged log_[32];static unsigned calls;
static uint32_t node_result,alloc_result,rngs[4];static unsigned rng_index;static int clear_pose;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49630c)){CHECK(n==0&&r->eax==G(0x4989e8));w32(0,F(0x24),0xcafef00du);r->eax=0x1234u;r->edx=0x2345u;r->ecx=0xabcd4321u;}
    else if(f==G(0x47ebb8)){CHECK(n==2);r->eax=0xeb10eb11u;r->edx=0xeb2u;r->ecx=0xeb3u;}
    else if(f==S20){CHECK(n==2);r->eax=node_result;r->edx=0x2002u;r->ecx=0x2003u;}
    else if(f==G(0x47fa20)){CHECK(n==3);for(unsigned i=0;i<32;++i)((uint8_t *)host(0,a[2]))[i]=(uint8_t)(0x40+i);
        r->eax=0xfa21u;r->edx=0xfa22u;r->ecx=0xfa23u;}
    else if(f==G(0x40f540)){CHECK(n==1);r->eax=MOUNT;r->edx=0xf542u;r->ecx=0xf543u;}
    else if(f==G(0x4801d4)){CHECK(n==3);w32(0,a[2],0x58000001u);w32(0,a[2]+4,0x58000002u);w32(0,a[2]+8,0x58000003u);
        if(clear_pose)w16(0,A+0x32,0);
        r->eax=0x01d1u;r->edx=0x48010000u;r->ecx=0x01d3u;}
    else if(f==S24){CHECK(n==1);r->eax=AIM;r->edx=0x2402u;r->ecx=0x2403u;}
    else if(f==G(0x48043c)){CHECK(n==3);w32(0,a[2],0x84000001u);w32(0,a[2]+4,0x84000002u);w32(0,a[2]+8,0x84000003u);
        r->eax=0x0431u;r->edx=0x0432u;r->ecx=0x0433u;}
    else if(f==G(0x492990)){CHECK(n==3);w16(0,a[0],0x1111);w16(0,a[0]+2,0x2222);w16(0,a[0]+4,0x3333);
        r->eax=0x2991u;r->edx=0x2992u;r->ecx=0x2993u;}
    else if(f==G(0x471b30)){CHECK(n==1);r->eax=alloc_result;r->edx=0x1b32u;r->ecx=0x1b33u;}
    else if(f==G(0x408940)){CHECK(n==5);l->context=r16(0,F(0x14));r->eax=0x8941u;r->edx=0x8942u;r->ecx=0x89430000u;}
    else if(f==G(0x4089e4)){CHECK(n==4);r->eax=0x89e1u;r->edx=0x89e2u;r->ecx=0x89e3u;}
    else if(f==G(0x47f9a0)){CHECK(n==2);w16(0,a[1],0x4444);w16(0,a[1]+2,0x5555);w16(0,a[1]+4,0x6666);
        r->eax=0xf9a1u;r->edx=0xf9a2u;r->ecx=0xf9a3u;}
    else if(f==G(0x40f57c)){CHECK(n==3);r->eax=0xf571u;r->edx=0xf572u;r->ecx=0xf573u;}
    else if(f==G(0x40ee0c)){CHECK(n==4);r->eax=0xee01u;r->edx=0xee02u;r->ecx=0xee03u;}
    else if(f==G(0x47e0b0)){CHECK(n==2);r->eax=(uint32_t)(((int64_t)(int32_t)a[0]*(int32_t)a[1])>>10);r->edx=0x7e07e0u;r->ecx=0x7e1u;}
    else if(f==G(0x492ee0)){CHECK(n==1&&a[0]==G(0x4d2599));r->eax=rngs[rng_index++&3];r->edx=0x2ee1u+rng_index;r->ecx=0x2ee2u;}
    else {fprintf(stderr,"weapon_eject: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0,sizeof(arena));memset(scratch,0x5a,sizeof(scratch));memset(globals1,0,sizeof(globals1));memset(globals2,0,sizeof(globals2));
    calls=0;rng_index=0;clear_pose=0;node_result=N;alloc_result=O;
    w32(0,W+0x10,0x10101010u);w32(0,W+0x18,D);w32(0,W+0x1c,C1C);w32(0,W+0x20,TYPE);
    w16(0,D,0x0102);arena[D-OBJ+6]=2;w16(0,D+0x18,0x0333);w16(0,C1C+0x26,3);
    w32(0,A,VTA);w32(0,VTA+0x24,S24);w32(0,A+0x34,S);w32(0,S+4,R);w32(0,S+0x16,NODES);w32(0,R,VTR);w32(0,VTR+0x20,S20);
    w16(0,N+4,2);w32(0,TABLE+12,0x77770003u);
    w32(0,G(0x4a9642),0x96429642u);w32(0,G(0x4a98f4),TABLE);w32(0,G(0x4a9868),0x98689868u);
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,40);
}
/* A full eject (flag 0): lazy matrix, node 2, debris object. */
static void eject_checks(void){
    es2_weapon_graph g=graph();reset();uint32_t ecx=0;
    uint64_t v=es2_weapon_eject_registers(&memory,&g,W,A,0x10000u,entry,&ecx);
    CHECK(calls==13&&ecx==0xcafef00du);
    CHECK(log_[0].f==G(0x49630c)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x47ebb8)&&log_[1].a[0]==A+0xc&&log_[1].a[1]==A+0x12&&log_[1].r[0]==A+0x12&&log_[1].r[1]==A+0xc&&log_[1].r[2]==0x0102);
    CHECK(log_[2].f==S20&&log_[2].a[0]==R&&log_[2].a[1]==0xeb100102u&&log_[2].r[0]==R&&log_[2].r[1]==VTR&&log_[2].r[2]==0xeb3u);
    CHECK(log_[3].f==G(0x47fa20)&&log_[3].a[0]==NODES+64&&log_[3].a[1]==A+0x12&&log_[3].a[2]==F(0x78));
    CHECK(log_[3].r[0]==NODES+64&&log_[3].r[1]==A+0x12&&log_[3].r[2]==NODES+64);
    CHECK(log_[4].f==G(0x40f540)&&log_[4].a[0]==W&&log_[4].r[0]==0xfa21u&&log_[4].r[1]==0xfa22u&&log_[4].r[2]==0xfa23u);
    CHECK(log_[5].f==G(0x4801d4)&&log_[5].a[0]==MOUNT&&log_[5].a[1]==F(0x78)&&log_[5].a[2]==F(0x58));
    CHECK(log_[5].r[0]==MOUNT&&log_[5].r[1]==F(0x78)&&log_[5].r[2]==F(0x58));
    /* A+32h is 1 now: no second euler_matrix; EDX = lo(4801d4's, 3), ECX = the class byte. */
    CHECK(log_[6].f==S24&&log_[6].a[0]==A&&log_[6].r[0]==VTA&&log_[6].r[1]==0x48010003u&&log_[6].r[2]==2&&r16(0,F(0x32))==3);
    CHECK(log_[7].f==G(0x48043c)&&log_[7].a[0]==AIM+0x14&&log_[7].a[1]==A+0x12&&log_[7].a[2]==F(0x84));
    CHECK(log_[7].r[0]==AIM+0x14&&log_[7].r[1]==F(0x84)&&log_[7].r[2]==A);
    CHECK(log_[8].f==G(0x492990)&&log_[8].a[0]==F(0x38)&&log_[8].a[1]==F(0x58)&&log_[8].a[2]==F(0x84));
    CHECK(log_[8].r[0]==F(0x58)&&log_[8].r[1]==F(0x38)&&log_[8].r[2]==F(0x84));
    CHECK(log_[9].f==G(0x471b30)&&log_[9].a[0]==0x96429642u&&log_[9].r[0]==0x96429642u&&log_[9].r[1]==0x2992u&&log_[9].r[2]==0x2993u);
    CHECK(log_[10].f==G(0x408940)&&log_[10].a[0]==O&&log_[10].a[1]==0x77770003u&&log_[10].a[2]==F(0x58)&&log_[10].a[3]==2&&log_[10].a[4]==0x14);
    CHECK(log_[10].r[0]==TABLE&&log_[10].r[1]==0x77770003u&&log_[10].r[2]==O&&log_[10].context==0x14);
    CHECK(log_[11].f==G(0x4089e4)&&log_[11].a[0]==O&&log_[11].a[1]==0x4b0&&log_[11].a[2]==((O&0xffff0000u)|0x0333)&&log_[11].a[3]==0x89433333u);
    CHECK(log_[11].r[0]==D&&log_[11].r[1]==log_[11].a[2]&&log_[11].r[2]==0x89433333u);
    CHECK(log_[12].f==G(0x47f9a0)&&log_[12].a[0]==F(0x78)&&log_[12].a[1]==F(0x38)&&log_[12].r[0]==F(0x78)&&log_[12].r[1]==0x89e2u&&log_[12].r[2]==F(0x38));
    CHECK(v==((uint64_t)0x55556666u<<32|(O+0xc)));
    CHECK(r32(0,O+0xc)==0x55554444u&&r16(0,O+0x10)==0x6666&&r16(0,O+0x32)==0&&r32(0,O+0x50)==0x98689868u);
    CHECK(r32(0,W+0x10)==0&&arena[W-OBJ+0x49]==1&&r16(0,A+0x32)==1&&r32(0,COUNTER)==36&&r16(0,F(0x14))==8&&r16(0,F(0x26))==0x0102);
}
/* Already ejected; no debris for class 4; node -1 and null use 6c56a8h;
 * a cleared A+32h gives a second euler_matrix; a failed allocation; the
 * flag's low word decides (-1, -1). */
static void eject_paths(void){
    es2_weapon_graph g=graph();reset();uint32_t ecx=0;arena[W-OBJ+0x49]=7;
    uint64_t v=es2_weapon_eject_registers(&memory,&g,W,A,0,entry,&ecx);
    CHECK(calls==1&&v==((uint64_t)(W+0x43)<<32|(W+0x43))&&r32(0,W+0x10)==0x10101010u&&ecx==0xcafef00du);
    reset();arena[D-OBJ+6]=4;w16(0,A+0x32,9);w16(0,N+4,0xffff);
    v=es2_weapon_eject_registers(&memory,&g,W,A,0,entry,&ecx);
    CHECK(calls==5&&log_[1].f==S20&&log_[1].a[1]==((W+0x43)&0xffff0000u|0x0102)&&log_[1].r[2]==0x0102);
    CHECK(log_[2].a[0]==G(0x6c56a8)&&v==((uint64_t)0x48010000u<<32|D)&&arena[W-OBJ+0x49]==1);
    reset();arena[D-OBJ+6]=0x80;node_result=0;clear_pose=1;alloc_result=0;
    v=es2_weapon_eject_registers(&memory,&g,W,A,0,entry,&ecx);
    CHECK(calls==11&&log_[3].a[0]==G(0x6c56a8)&&log_[6].f==G(0x47ebb8)&&log_[6].r[0]==A+0x12&&log_[6].r[1]==A+0xc&&log_[6].r[2]==0xffffff80u);
    CHECK(log_[7].f==S24&&log_[7].r[1]==0xeb2u&&log_[7].r[2]==0xeb3u&&r16(0,A+0x32)==1&&v==0&&r32(0,COUNTER)==40);
    reset();
    v=es2_weapon_eject_registers(&memory,&g,W,A,0xffff0001u,entry,&ecx);
    CHECK(calls==13&&log_[10].a[3]==0xffffffffu&&log_[10].a[4]==0xffffffffu);
}
static void slot_checks(void){
    es2_weapon_graph g=graph();reset();
    /* 40ee0c */
    uint64_t v=es2_weapon_damage_registers(&memory,&g,W,A,5,0x100,entry);
    CHECK(calls==1&&log_[0].f==G(0x40f57c)&&log_[0].a[0]==W&&log_[0].a[1]==A&&log_[0].a[2]==0);
    CHECK(log_[0].r[0]==W&&log_[0].r[1]==W+0x43&&log_[0].r[2]==A&&v==((uint64_t)0xf572u<<32|0xf571u));
    reset();v=es2_weapon_damage_registers(&memory,&g,W,A,5,0x1100,entry);CHECK(calls==0&&v==((uint64_t)entry[1]<<32|W));
    reset();arena[W-OBJ+0x49]=1;v=es2_weapon_damage_registers(&memory,&g,W,A,5,0x100,entry);CHECK(calls==0&&v==((uint64_t)(W+0x43)<<32|W));
    /* 40ee38: q = 3, rem 7, multiply_q10(14h, 300) = 5 */
    reset();v=es2_beam_damage_registers(&memory,&g,W,A,9,0x80+82,entry);
    CHECK(calls==2&&log_[0].f==G(0x40ee0c)&&log_[0].a[2]==9&&log_[0].a[3]==0x80+82&&log_[0].r[0]==9&&log_[0].r[1]==A&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x47e0b0)&&log_[1].a[0]==0x14&&log_[1].a[1]==300&&log_[1].r[0]==300&&log_[1].r[1]==7&&log_[1].r[2]==0x19);
    CHECK(r32(0,W+0x7f)==15&&v==((uint64_t)0x7e07e0u<<32|5));
    reset();v=es2_beam_damage_registers(&memory,&g,W,A,9,0x80,entry);CHECK(calls==1&&r32(0,W+0x7f)==0x14&&v==((uint64_t)0xee02u<<32|0xee01u));
    /* 40ef6c */
    reset();v=es2_targ_damage_registers(&memory,&g,W,A,9,0x12345,entry);
    CHECK(calls==1&&log_[0].r[0]==9&&log_[0].r[1]==A&&r16(0,W+0x7f)==0x2345&&r16(0,W+0x81)==0&&v==((uint64_t)0xee02u<<32|0xee01u));
}
static void gun_checks(void){
    es2_weapon_graph g=graph();reset();
    uint64_t v=es2_gun_damage_registers(&memory,&g,W,A,0x80+25,0x80+103,entry);
    CHECK(calls==1&&log_[0].r[0]==A&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]&&v==((uint64_t)0xee02u<<32|0xee01u));
    /* type 0, counters 1..3: a miss, then a hit; the eject at counter 3. */
    reset();w16(0,W+0x7b,1);rngs[0]=0x0000ffffu;rngs[1]=0x1234012bu;
    v=es2_gun_damage_registers(&memory,&g,W,A,0x80+25,0x80+103,entry);
    CHECK(calls==4&&log_[1].f==G(0x492ee0)&&log_[1].r[0]==1&&log_[1].r[1]==0&&log_[1].r[2]==0x19);
    CHECK(log_[2].f==G(0x492ee0)&&log_[2].r[0]==2&&log_[2].r[1]==0x2ee2u&&log_[2].r[2]==G(0x4d2599));
    CHECK(log_[3].f==G(0x40f57c)&&log_[3].r[0]==3&&log_[3].r[1]==A&&log_[3].r[2]==G(0x4d2599)&&v==((uint64_t)0xf572u<<32|0xf571u));
    /* x below 80h by more than 25: the counter starts at -1; no hit. */
    reset();w16(0,W+0x7b,1);rngs[0]=0x3ff;
    v=es2_gun_damage_registers(&memory,&g,W,A,0,0x80+10,entry);
    CHECK(calls==2&&log_[1].r[0]==0xffffffffu&&log_[1].r[1]==0xfffffffdu&&log_[1].r[2]==0x19&&v==((uint64_t)0x2ee2u<<32|0));
    reset();w16(0,W+0x7b,1);v=es2_gun_damage_registers(&memory,&g,W,A,0,0x80,entry);CHECK(calls==1&&v==((uint64_t)TYPE<<32|0xee01u));
    /* type 2: q = 2, W+63h = 400h - 204 */
    reset();w16(0,W+0x7b,1);w16(0,TYPE,2);v=es2_gun_damage_registers(&memory,&g,W,A,0,0x80+50,entry);
    CHECK(calls==1&&r16(0,W+0x63)==0x400-204&&v==((uint64_t)2<<32|204));
    reset();w16(0,W+0x7b,1);w16(0,TYPE,3);v=es2_gun_damage_registers(&memory,&g,W,A,0,0x80+50,entry);
    CHECK(calls==1&&r16(0,W+0x63)==0&&v==((uint64_t)TYPE<<32|0xee01u));
}
int main(void){
    eject_checks();eject_paths();slot_checks();gun_checks();
    printf("PASS: weapon_eject %u checks\n",checks);return 0;
}
