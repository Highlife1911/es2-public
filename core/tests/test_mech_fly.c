#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mech_fly.h"
/* Synthetic guest: the MECH (motion record R = A+2B9h), its region flag words,
 * a target and a debris record at 90000000h, an image base I with the packet
 * and probe statics 0x49a158..0x49a1cf and the terrain map pointer 0x4a0be0, a
 * frame scratch at A0000000h. Every game call is logged with its arguments and
 * entry registers; the stubs answer from small plans. The expectations are read
 * from the reference listing (hit_tests.md, "MECH player fly
 * movement"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_MECH_FLY_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_MECH_FLY_SCRATCH],statics[0x80],mapslot[4];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mech_fly line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x49a158)<sizeof(statics))return statics+(a-G(0x49a158));
    if(a-G(0x4a0be0)<sizeof(mapslot))return mapslot+(a-G(0x4a0be0));
    fprintf(stderr,"mech_fly: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define A (OBJ+0x100)
#define R (A+0x2b9)
#define FLAGS (OBJ+0x500)
#define T (OBJ+0x600)
#define REC (OBJ+0x700)
#define MAP UINT32_C(0x4a4a4a4a)
#define P G(0x49a170)
#define STAT(va) ((va)-0x49a158u)
typedef struct logged { uint32_t f,a[4],r[3],x[3]; unsigned n; } logged;
static logged log_[64];static unsigned calls;
static int32_t heights[8],dist;static uint32_t sweeps[8],clear_after[8];static unsigned height_index,sweep_index,part_index;
static int32_t s32(uint32_t x){return (int32_t)x;}
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls++;memset(l,0,sizeof(*l));l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x47ebb8)){CHECK(n==2);r->eax=0xeb01u;r->edx=0xeb02u;r->ecx=0xeb03u;}
    else if(f==G(0x48043c)){ /* out = source + the matrix translation */
        CHECK(n==3);for(unsigned i=0;i<3;++i){l->x[i]=r32(0,a[0]+4*i);w32(0,a[2]+4*i,l->x[i]+r32(0,a[1]+0x14+4*i));}
        r->eax=0x0c01u+calls;r->edx=0x0c02u+calls;r->ecx=0x0c03u+calls;}
    else if(f==G(0x467924)){CHECK(n==1);r->eax=0x67000000u|(uint16_t)(a[0]*2);r->edx=0x6702u;r->ecx=0x6703u;}
    else if(f==G(0x46e180)){ /* the height relative to the point's Z */
        CHECK(n==2&&a[0]==MAP);for(unsigned i=0;i<3;++i)l->x[i]=r32(0,a[1]+4*i);
        r->eax=l->x[2]+(uint32_t)heights[height_index++];r->edx=0x6e02u;r->ecx=0x6e03u;}
    else if(f==G(0x426528)){ /* the segment start and length */
        CHECK(n==1&&a[0]==P&&r32(0,r32(0,P))==F(0xdc));uint32_t s=r32(0,P);
        l->x[0]=r32(0,F(0xdc)+0x14);l->x[1]=r32(0,s+4);l->x[2]=r16(0,s+8);
        r->eax=sweeps[sweep_index++];r->edx=0x2602u;r->ecx=0x2603u;}
    else if(f==G(0x47e0b0)){CHECK(n==2);r->eax=(uint32_t)(((int64_t)s32(a[0])*s32(a[1]))>>10);r->edx=0xe002u;r->ecx=0xe003u;}
    else if(f==G(0x4188c8)){ /* the part hit; may clear a region flag word */
        CHECK(n==4&&a[0]==A&&a[2]==P);for(unsigned i=0;i<3;++i)l->x[i]=r32(0,a[3]+4*i);
        if(clear_after[part_index])w16(0,FLAGS+clear_after[part_index]-1,0);
        ++part_index;r->eax=0x8801u;r->edx=0x8802u;r->ecx=0x8803u;}
    else if(f==G(0x4089bc)){CHECK(n==1);l->x[0]=r16(0,a[0]);w16(0,a[0],1);r->eax=REC;r->edx=0x89b2u;r->ecx=0x89b3u;}
    else if(f==G(0x408530)){CHECK(n==4);for(unsigned i=0;i<3;++i)l->x[i]=r32(0,a[2]+4*i);r->eax=0x8531u;r->edx=0x8532u;r->ecx=0x8533u;}
    else if(f==G(0x462ac4)){CHECK(n==1);r->eax=0xac1u;r->edx=0xac2u;r->ecx=0xac3u;}
    else if(f==G(0x47de72)){CHECK(n==3);r->eax=(uint32_t)dist;r->edx=0xde72u;r->ecx=0xde73u;}
    else if(f==G(0x463114)){CHECK(n==2);r->eax=0x3111u;r->edx=0x3112u;r->ecx=0x3113u;}
    else if(f==G(0x46297c)){CHECK(n==2);r->eax=0x2971u;r->edx=0x2972u;r->ecx=0x2973u;}
    else if(f==G(0x49288c)){CHECK(n==2);r->eax=0x12345u;r->edx=0x8c02u;r->ecx=0x8c03u;}
    else if(f==G(0x41a74c)){CHECK(n==2);r->eax=0x74c10000u|0x5555u;r->edx=0x74c2u;r->ecx=0x74c3u;}
    else {fprintf(stderr,"mech_fly: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static unsigned find(uint32_t f,unsigned from){for(unsigned i=from;i<calls;++i)if(log_[i].f==f)return i;return calls;}
static void reset(void){
    memset(arena,0,sizeof(arena));memset(scratch,0x5a,sizeof(scratch));memset(statics,0,sizeof(statics));
    w32(0,G(0x4a0be0),MAP);w32(0,G(0x49a17a),G(0x49a158));
    calls=0;height_index=sweep_index=part_index=0;memset(heights,0,sizeof(heights));memset(sweeps,0,sizeof(sweeps));
    memset(clear_after,0,sizeof(clear_after));dist=100;
    w32(0,A+0x20e,FLAGS);for(unsigned i=0;i<16;++i)w16(0,FLAGS+2*i,1);
    /* angles, pose (translation = position) */
    w16(0,A+0xc,0x100);w16(0,A+0xe,0x200);
    for(unsigned i=0;i<9;++i)w16(0,A+0x12+2*i,(uint16_t)(0x1000+i));arena[A-OBJ+0x24]=2;arena[A-OBJ+0x25]=0x99;
    w32(0,A+0x26,50000);w32(0,A+0x2a,60000);w32(0,A+0x2e,7000);w16(0,A+0x32,1);
    w32(0,R,1);w32(0,R+4,800);w32(0,R+8,3);w32(0,R+0xc,0x10010);w32(0,R+0x10,0xfffe0020u);w32(0,R+0x14,0x30);
    w16(0,R+0x18,0xfff0);w16(0,R+0x1a,0x40);
    w32(0,REC,OBJ+0xc00);w32(0,REC+6,OBJ+0xd00);
}
/* The first call: the probes, the lazy matrix, the integration, a terrain
 * contact for part 8, a sweep hit for part 7, a sweep miss for part 4, a
 * sweep hit for part 5 (with the part-4 point), the nose and the slope
 * below the terrain, no ground contact, the player's sound, no target. */
static void first_checks(void){
    es2_weapon_graph g=graph();reset();w16(0,A+0x32,0);arena[A-OBJ+0xa3]=1;
    heights[0]=+40;heights[1]=-1;heights[2]=-5;heights[3]=0;heights[4]=-100;
    sweeps[0]=7;sweeps[1]=0;sweeps[2]=1;sweeps[3]=0;dist=2346;
    uint64_t v=es2_mech_fly_registers(&memory,&g,A,entry);
    /* The probe offsets and their guards. */
    CHECK(r32(0,G(0x49a188))==1000&&r32(0,G(0x49a18c))==0xfffffd44u&&r32(0,G(0x49a190))==0xffffff9cu&&statics[STAT(0x49a194)]==1);
    CHECK(r32(0,G(0x49a195))==0xfffffc18u&&r32(0,G(0x49a1a2))==450&&r32(0,G(0x49a1af))==0xfffffe3eu&&r32(0,G(0x49a1c0))==1000);
    CHECK(statics[STAT(0x49a1a1)]==1&&statics[STAT(0x49a1ae)]==1&&statics[STAT(0x49a1bb)]==1&&statics[STAT(0x49a1c8)]==1);
    /* euler_matrix(A+0Ch, A+12h): EAX = A+12h, EDX = A+0Ch, ECX = 0 (the
     * fifth probe block cleared it). */
    CHECK(log_[0].f==G(0x47ebb8)&&log_[0].a[0]==A+0xc&&log_[0].a[1]==A+0x12&&log_[0].r[0]==A+0x12&&log_[0].r[1]==A+0xc&&log_[0].r[2]==0);
    CHECK(r16(0,A+0x32)==0&&r32(0,F(4))==A+0xc);
    /* The pose copy and the five probes: EAX = f-DCh, EDX = Y, ECX = out. */
    CHECK(r16(0,F(0xdc))==0x1000&&r16(0,F(0xcc))==0x1008&&scratch[ES2_MECH_FLY_FRAME-0xca]==2&&scratch[ES2_MECH_FLY_FRAME-0xc9]==0x99);
    CHECK(log_[1].f==G(0x48043c)&&log_[1].a[0]==G(0x49a188)&&log_[1].a[1]==F(0xdc)&&log_[1].a[2]==F(0x8c));
    CHECK(log_[1].r[0]==F(0xdc)&&log_[1].r[1]==60000&&log_[1].r[2]==F(0x8c));
    CHECK(log_[2].a[0]==G(0x49a195)&&log_[2].a[2]==F(0x98)&&log_[2].r[0]==0x0c01u+2&&log_[2].r[1]==F(0x98)&&log_[2].r[2]==F(0xdc));
    CHECK(log_[3].a[0]==G(0x49a1a2)&&log_[3].a[2]==F(0xbc)&&log_[3].r[0]==F(0xbc)&&log_[3].r[1]==F(0xdc)&&log_[3].r[2]==0x0c03u+3);
    CHECK(log_[4].a[0]==G(0x49a1af)&&log_[4].a[2]==F(0xb0)&&log_[4].r[0]==F(0xdc)&&log_[4].r[1]==0x0c02u+4&&log_[4].r[2]==F(0xb0));
    CHECK(log_[5].a[0]==G(0x49a1bc)&&log_[5].a[2]==F(0xa4)&&log_[5].r[0]==0x0c01u+5&&log_[5].r[1]==F(0xa4)&&log_[5].r[2]==F(0xdc));
    /* movement_step: the slots are R+0Ch, lo(first EAX, R+10h), lo(second
     * slot, R+14h); the position moves by the sign-extended results. */
    CHECK(log_[6].f==G(0x467924)&&log_[6].a[0]==0x10010u&&log_[6].r[0]==0x30&&log_[6].r[1]==0x10010u&&log_[6].r[2]==0xfffe0020u);
    CHECK(log_[7].a[0]==0x67000020u&&log_[7].r[1]==0x6702u&&log_[7].r[2]==0x20u);
    CHECK(log_[8].a[0]==0x67000030u&&log_[8].r[0]==0x67000040u&&log_[8].r[1]==0x40u&&log_[8].r[2]==0x67000030u);
    CHECK(r32(0,A+0x26)==50000+0x20&&r32(0,A+0x2a)==60000+0x40&&r32(0,A+0x2e)==7000+0x60);
    /* The packet. */
    CHECK(r32(0,P)==F(0x114)&&r32(0,F(0x114))==F(0xdc)&&r16(0,P+6)==8000&&r16(0,P+8)==0&&r32(0,P+0xe)==0);
    CHECK(r16(0,P+0x12)==5&&r32(0,P+0x14)==A&&r32(0,P+0xa)==G(0x49a158)&&r32(0,F(8))==FLAGS);
    /* Part 8: the probe point (copy translation = the old position) is
     * 40 below the terrain: no sweep, roll rate -= (4000*40)>>10 = 156,
     * P+4 = (800*500)>>10 = 390, Z raised, part hit at f-80h. */
    unsigned i=find(G(0x46e180),0);
    CHECK(log_[i-1].f==G(0x48043c)&&log_[i-1].a[0]==G(0x49a188)&&log_[i-1].a[2]==F(0x80)&&log_[i-1].r[0]==FLAGS&&log_[i-1].r[1]==F(0x80));
    CHECK(log_[i].a[1]==F(0x80)&&log_[i].x[0]==51000&&log_[i].x[1]==59300&&log_[i].x[2]==6900&&log_[i].r[0]==F(0x80)&&log_[i].r[1]==MAP);
    CHECK(log_[i+1].f==G(0x47e0b0)&&log_[i+1].a[0]==4000&&log_[i+1].a[1]==40&&log_[i+1].r[0]==40&&log_[i+1].r[1]==40&&log_[i+1].r[2]==59300);
    CHECK(log_[i+2].f==G(0x47e0b0)&&log_[i+2].a[0]==800&&log_[i+2].a[1]==500&&log_[i+2].r[0]==800&&log_[i+2].r[1]==0xe002u&&log_[i+2].r[2]==800);
    CHECK(log_[i+3].f==G(0x4188c8)&&log_[i+3].a[1]==8&&log_[i+3].a[3]==F(0x80)&&log_[i+3].x[2]==6940);
    CHECK(log_[i+3].r[0]==A+0xc&&log_[i+3].r[1]==((0xe002u&0xffff0000u)|(uint16_t)(0x200+0x40-156))&&log_[i+3].r[2]==F(0x80));
    /* Part 7: the probe goes through the copy whose translation is now
     * the part-8 point; the terrain is 1 below: a sweep (hit) from the
     * part-7 point, roll rate += 4000, P+4 = 1000. */
    i=find(G(0x46e180),i+1);
    CHECK(log_[i-1].a[0]==G(0x49a195)&&log_[i].x[0]==51000-1000&&log_[i].x[1]==59300-700&&log_[i].x[2]==6900-100);
    CHECK(log_[i+1].f==G(0x426528)&&log_[i+1].x[0]==50000-1000&&log_[i+1].x[1]==800&&log_[i+1].x[2]==300);
    CHECK(log_[i+1].r[0]==800&&log_[i+1].r[1]==50000-1000&&log_[i+1].r[2]==60000-700);
    CHECK(log_[i+2].f==G(0x4188c8)&&log_[i+2].a[1]==7&&log_[i+2].r[0]==A+0xc&&log_[i+2].r[1]==((0x2602u&0xffff0000u)|(uint16_t)(0x200+(0x40-156)+(0x40-156+4000))));
    CHECK(log_[i+2].r[2]==F(0x80)&&log_[i+2].x[2]==6800);
    /* Part 4: a sweep miss from the part-4 point; part 5: a hit from the
     * part-5 point, roll rate -= 8000, and the part hit gets f-B0h. */
    CHECK(log_[i+3].f==G(0x426528)&&log_[i+3].x[0]==50000-450&&log_[i+3].x[2]==150&&log_[i+3].r[0]==r32(0,F(0xac))&&log_[i+3].r[1]==800);
    CHECK(log_[i+4].f==G(0x426528)&&log_[i+4].x[0]==50000+450&&log_[i+4].r[0]==800&&log_[i+4].r[1]==50000+450&&log_[i+4].r[2]==60000-500);
    CHECK(log_[i+5].f==G(0x4188c8)&&log_[i+5].a[1]==5&&log_[i+5].a[3]==F(0xb0)&&log_[i+5].x[0]==50000-450&&log_[i+5].r[0]==F(0xb0));
    CHECK(log_[i+5].r[1]==((0x2602u&0xffff0000u)|(uint16_t)(0x200+(0x40-156)+(0x40-156+4000)+(0x40-156+4000-8000)))&&log_[i+5].r[2]==A+0xc);
    CHECK(r16(0,P+4)==5000&&r16(0,R+0x1a)==(uint16_t)(0x40-156+4000-8000)&&r16(0,A+0xe)==236&&r32(0,F(0x78))==7096);
    /* The nose: terrain 5 below, sweep miss. The slope: terrain level
     * (not above). The ground clamp: 100 below, no contact. */
    i=find(G(0x46e180),i+6);
    CHECK(log_[i-1].a[0]==G(0x49a1bc)&&log_[i-1].r[0]==F(0xdc)&&log_[i-1].r[1]==FLAGS&&log_[i].r[2]==MAP&&log_[i].r[1]==F(0x80));
    CHECK(log_[i+1].f==G(0x426528)&&log_[i+1].x[2]==200&&log_[i+1].x[0]==50000);
    CHECK(log_[i+2].f==G(0x48043c)&&log_[i+2].a[0]==F(0x120)&&log_[i+2].r[0]==F(0x80)&&log_[i+2].r[1]==F(0xdc)&&log_[i+2].r[2]==F(0x120));
    CHECK(r32(0,F(0x11c))==15000&&r32(0,F(0x118))==0xfffffa24u&&log_[i+3].f==G(0x46e180));
    CHECK(log_[i+4].f==G(0x46e180)&&log_[i+4].x[0]==50000+0x20&&log_[i+4].x[2]==7000+0x60&&log_[i+4].r[0]==60000+0x40&&log_[i+4].r[2]==MAP);
    /* Sound: 16*2346 + 6D60h = 10000h >= FFFFh, clamped. */
    CHECK(log_[i+5].f==G(0x47de72)&&log_[i+5].a[0]==1&&log_[i+5].a[1]==800&&log_[i+5].a[2]==3&&log_[i+5].r[0]==800&&log_[i+5].r[1]==1);
    CHECK(log_[i+6].f==G(0x463114)&&log_[i+6].a[0]==0x2d&&log_[i+6].a[1]==0xffff&&log_[i+6].r[0]==0xffff&&log_[i+6].r[2]==2346);
    CHECK(log_[i+7].f==G(0x46297c)&&log_[i+7].a[1]==A+0x26&&log_[i+7].r[0]==0x3111u&&log_[i+7].r[2]==A+0x26);
    CHECK(log_[i+8].f==G(0x41a74c)&&log_[i+8].a[0]==A&&log_[i+8].a[1]==0&&log_[i+8].r[0]==0&&log_[i+8].r[1]==0x2972u);
    CHECK(calls==i+9&&v==((uint64_t)0x74c2u<<32|0x74c10001u));
}
/* A later call: the guards hold, no lazy matrix, no integration (A+A4h);
 * only the nose and the leg regions; the nose touches the terrain and its
 * part hit clears the nose region: debris (f-3Ch = 1 variant) and the
 * dead byte; the ground clamp touches and clears the leg region: debris
 * again; the dead player's sound; a target. */
static void later_checks(void){
    es2_weapon_graph g=graph();reset();for(unsigned i=0;i<5;++i)statics[STAT(0x49a194)+13*i]=1;
    arena[A-OBJ+0xa4]=1;arena[A-OBJ+0xa3]=1;arena[A-OBJ+0x99]=1;w32(0,A+0x1a4,T);
    w16(0,FLAGS+0x10,0);w16(0,FLAGS+0xe,0);w16(0,FLAGS+8,0);w16(0,FLAGS+0xa,0);
    heights[0]=+30;heights[1]=+3;clear_after[0]=1;clear_after[1]=0xd;
    uint64_t v=es2_mech_fly_registers(&memory,&g,A,entry);
    CHECK(r32(0,G(0x49a188))==0&&log_[0].f==G(0x48043c)&&log_[0].r[1]==60000);
    /* The nose: pitch rate FFF0h clamped to 0, += (2000*30)>>10 = 58,
     * P+4 = (800*1000)>>10 = 781. */
    unsigned i=find(G(0x46e180),0);
    CHECK(log_[i+1].f==G(0x47e0b0)&&log_[i+1].a[0]==2000&&log_[i+1].a[1]==30&&log_[i+1].r[0]==800&&log_[i+1].r[1]==30&&log_[i+1].r[2]==30);
    CHECK(log_[i+2].a[0]==800&&log_[i+2].a[1]==1000&&log_[i+2].r[0]==800&&log_[i+2].r[1]==800);
    CHECK(log_[i+3].f==G(0x4188c8)&&log_[i+3].a[1]==0&&log_[i+3].r[0]==F(0x80)&&log_[i+3].r[2]==0&&r32(0,F(0x3c))==1);
    CHECK(log_[i+3].r[1]==((0xe002u&0xffff0000u)|(uint16_t)(0x100+58))&&r16(0,R+0x18)==58&&r16(0,A+0xc)==0x100+58);
    /* Debris, f-3Ch = 1: w at f-3Eh, T at f-44h; EAX = 10*w, EDX = the
     * table, ECX = w (w lowered to 1 by the stub). */
    CHECK(log_[i+4].f==G(0x4089bc)&&log_[i+4].a[0]==F(0x3e)&&log_[i+4].x[0]==3&&log_[i+4].r[0]==0x8801u&&log_[i+4].r[1]==FLAGS&&log_[i+4].r[2]==F(0x3e));
    CHECK(log_[i+5].f==G(0x408530)&&log_[i+5].a[0]==OBJ+0xc00+10&&log_[i+5].a[1]==OBJ+0xd00&&log_[i+5].a[2]==F(0x80)&&log_[i+5].a[3]==0);
    CHECK(log_[i+5].r[0]==10&&log_[i+5].r[1]==OBJ+0xc0a&&log_[i+5].r[2]==1&&arena[A-OBJ+0xa4]==1&&r32(0,F(0x44))==REC);
    /* No slope (nose region gone). Ground clamp: 3 above: position Z
     * raised, P+4 = (800*5000)>>10 = 3906, part 6, debris (w at f-5Ah). */
    CHECK(log_[i+6].f==G(0x46e180)&&log_[i+6].r[0]==60000&&log_[i+6].r[1]==F(0x80)&&r32(0,A+0x2e)==7003);
    CHECK(log_[i+7].f==G(0x47e0b0)&&log_[i+7].a[0]==800&&log_[i+7].a[1]==5000&&log_[i+7].r[0]==800&&log_[i+7].r[1]==60000&&log_[i+7].r[2]==800);
    CHECK(log_[i+8].f==G(0x4188c8)&&log_[i+8].a[1]==6&&log_[i+8].r[0]==F(0x80)&&log_[i+8].r[1]==0xe002u&&log_[i+8].r[2]==0xe003u&&r16(0,P+4)==3906);
    CHECK(log_[i+9].f==G(0x4089bc)&&log_[i+9].a[0]==F(0x5a)&&log_[i+9].r[1]==FLAGS&&log_[i+9].r[2]==F(0x5a)&&r32(0,F(0x60))==REC&&r32(0,F(0x64))==A+0x92);
    CHECK(log_[i+10].f==G(0x408530)&&log_[i+10].r[0]==10&&log_[i+10].r[1]==OBJ+0xc0a&&log_[i+10].r[2]==1);
    /* Dead: 462ac4(2Dh) with EAX = EDX = ECX = A+92h (41a290); then dist_gate(A+26h, T+26h). */
    CHECK(log_[i+11].f==G(0x462ac4)&&log_[i+11].a[0]==0x2d&&log_[i+11].r[0]==A+0x92&&log_[i+11].r[1]==A+0x92&&log_[i+11].r[2]==A+0x92);
    CHECK(log_[i+12].f==G(0x49288c)&&log_[i+12].a[0]==A+0x26&&log_[i+12].a[1]==T+0x26&&log_[i+12].r[0]==T&&log_[i+12].r[1]==T+0x26&&log_[i+12].r[2]==A+0x26);
    CHECK(log_[i+13].f==G(0x41a74c)&&log_[i+13].a[1]==0x12345u&&log_[i+13].r[0]==0x12345u&&log_[i+13].r[2]==0x8c03u);
    CHECK(calls==i+14&&v==((uint64_t)0x74c2u<<32|0x74c10001u));
}
/* The nose by sweep hit with the region cleared: the second debris
 * variant (EAX = the table, EDX = 10*w); the slope; the sound clamp to 0. */
static void variant_checks(void){
    es2_weapon_graph g=graph();reset();for(unsigned i=0;i<5;++i)statics[STAT(0x49a194)+13*i]=1;
    arena[A-OBJ+0xa4]=1;arena[A-OBJ+0xa3]=1;w16(0,FLAGS+0x10,0);w16(0,FLAGS+0xe,0);w16(0,FLAGS+8,0);w16(0,FLAGS+0xa,0);
    heights[0]=-2;sweeps[0]=1;clear_after[0]=1;heights[1]=-1;dist=-1750;
    uint64_t v=es2_mech_fly_registers(&memory,&g,A,entry);
    unsigned i=find(G(0x426528),0);
    CHECK(log_[i+1].f==G(0x4188c8)&&log_[i+1].r[1]==((0x2602u&0xffff0000u)|(0x100+2000))&&r16(0,R+0x18)==2000&&r16(0,P+4)==5000);
    CHECK(r32(0,F(0x3c))==0&&r16(0,A+0xc)==0x100+2000);
    CHECK(log_[i+2].f==G(0x4089bc)&&log_[i+2].a[0]==F(0x46)&&log_[i+2].r[1]==F(0x46)&&log_[i+2].r[2]==0x8803u);
    CHECK(log_[i+3].f==G(0x408530)&&log_[i+3].a[0]==OBJ+0xc0a&&log_[i+3].r[0]==OBJ+0xc0a&&log_[i+3].r[1]==10&&log_[i+3].r[2]==1);
    /* The ground clamp is 1 below; the sound: 16*(-1750) + 6D60h = 0 -> 0. */
    CHECK(log_[i+4].f==G(0x46e180)&&log_[i+5].f==G(0x47de72)&&log_[i+6].f==G(0x463114)&&log_[i+6].a[1]==0&&log_[i+6].r[1]==0);
    CHECK(v==((uint64_t)0x74c2u<<32|0x74c10001u));
    /* The slope (the probe statics are 0 here: the guards were set by
     * hand): all three regions present, the point (0, 15000, -1500)
     * through the copy is 64 below the terrain: pitch rate (clamped to 0)
     * += (20*64)>>10 = 1, A+0Ch += 1. */
    reset();for(unsigned k=0;k<5;++k)statics[STAT(0x49a194)+13*k]=1;arena[A-OBJ+0xa4]=1;
    w16(0,FLAGS+0x10,0);w16(0,FLAGS+0xe,0);w16(0,FLAGS,0);
    sweeps[0]=0;sweeps[1]=0;heights[0]=-1;
    es2_mech_fly_registers(&memory,&g,A,entry);
    CHECK(calls==9&&log_[0].f==G(0x48043c)&&log_[5].f==G(0x426528)&&log_[6].f==G(0x426528)&&log_[7].f==G(0x46e180)&&log_[8].f==G(0x41a74c));
    reset();for(unsigned k=0;k<5;++k)statics[STAT(0x49a194)+13*k]=1;arena[A-OBJ+0xa4]=1;
    w16(0,FLAGS+0x10,0);w16(0,FLAGS+0xe,0);w16(0,FLAGS+8,1);w16(0,FLAGS+0xa,1);w16(0,FLAGS,1);
    sweeps[0]=0;sweeps[1]=0;sweeps[2]=0;heights[0]=-1;heights[1]=64;heights[2]=-1;
    es2_mech_fly_registers(&memory,&g,A,entry);
    i=find(G(0x47e0b0),0);
    CHECK(log_[i].a[0]==20&&log_[i].a[1]==64&&log_[i].r[0]==64&&log_[i].r[1]==0x6e02u&&log_[i].r[2]==0x6e03u);
    CHECK(r16(0,R+0x18)==1&&r16(0,A+0xc)==0x101&&r16(0,A+0x32)==0&&r32(0,F(0x54))==A+0xc&&log_[i-1].x[1]==15000+60000);
}
int main(void){
    first_checks();later_checks();variant_checks();
    printf("PASS: mech_fly %u checks\n",checks);return 0;
}
