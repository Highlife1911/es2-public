#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/base_update.h"
/* Synthetic guest: the building, its configuration, part records and
 * shape at 90000000h, an image base I with the globals and tables the five
 * functions read, a frame scratch at A0000000h. Every game call is logged
 * with its arguments and entry registers; the stubs return planned values.
 * The frame setup stub writes the record head 0CAFEF00Dh. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_BASE_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_BASE_SCRATCH],tables[0x40],globals1[0x100],globals2[0x10],globals3[0x10],counter[8];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"base_update line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4973f0)<sizeof(tables))return tables+(a-G(0x4973f0));
    if(a-G(0x4a9580)<sizeof(globals1))return globals1+(a-G(0x4a9580));
    if(a-G(0x4d1f40)<sizeof(globals2))return globals2+(a-G(0x4d1f40));
    if(a-G(0x6cbee0)<sizeof(globals3))return globals3+(a-G(0x6cbee0));
    if(a-OBJ-0x1000u<sizeof(counter))return counter+(a-OBJ-0x1000u);
    fprintf(stderr,"base_update: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; } logged;
static logged log_[64];static unsigned calls;
static uint32_t timer_eax,alloc_result,fire_result,rng_value;
#define B (OBJ+0x100)
#define C (OBJ+0x400)
#define Q (OBJ+0x500)
#define P (OBJ+0x600)
#define M (OBJ+0x700)
#define NODE (OBJ+0x720)
#define FRAMES (OBJ+0x740)
#define COUNTS (OBJ+0x760)
#define VTABLE (OBJ+0x780)
#define MODELS (OBJ+0x7c0)
#define SLOT74 UINT32_C(0x55667788)
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49630c)){CHECK(n==0);w32(0,FRAME+ES2_BASE_FRAME-(r->eax==G(0x497485)?0x2c:0x24),0xcafef00du);r->eax=0x1234u;}
    else if(f==G(0x467a48)){CHECK(n==1);r->eax=timer_eax;r->edx=0x5555aaaau;r->ecx=0x77u;}
    else if(f==G(0x403914)){CHECK(n==1);r->eax=0x0badu;r->edx=0x31u;r->ecx=0x32u;}
    else if(f==G(0x402604)){CHECK(n==2);r->eax=0x40u;r->edx=0x41u;}
    else if(f==G(0x46b430)){CHECK(n==1&&a[0]==0x5b);r->eax=fire_result;r->edx=0xabcd0042u;r->ecx=0x99u;}
    else if(f==G(0x46b48c)){CHECK(n==5);r->eax=0x61u;r->edx=0x62u;r->ecx=0x63u;}
    else if(f==G(0x471b30)){CHECK(n==1);r->eax=alloc_result;r->edx=0x71u;}
    else if(f==G(0x492f24)){CHECK(n==2&&a[0]==G(0x4d2599));r->eax=rng_value;r->edx=rng_value;r->ecx=0x12340000u|(uint16_t)a[1];}
    else if(f==G(0x48043c)||f==G(0x4801d4)){CHECK(n==3);for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,0x100u+i);r->eax=a[2];}
    else if(f==G(0x4089bc)){CHECK(n==1);r->eax=G(0x4a9650);r->edx=0x81u;r->ecx=a[0];}
    else if(f==G(0x408530)){CHECK(n==4);r->eax=0x91u;r->edx=0x92u;r->ecx=0x93u;}
    else if(f==SLOT74){CHECK(n==4);w16(0,P+11u*a[1],r16(0,Q+0x1eu*a[1]));r->eax=0xa1u;r->edx=0xa2u;r->ecx=0xa3u;}
    else if(f==G(0x403890)||f==G(0x4036c0)||f==G(0x40379c)||f==G(0x47ebb8)||f==G(0x407f1c)){r->eax=0xb0u+n;r->edx=0xc0u+n;}
    else {fprintf(stderr,"base_update: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={1,2,3};
static void building(unsigned parts){
    memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));
    w32(0,B,VTABLE);w32(0,VTABLE+0x74,SLOT74);w32(0,B+0x1f2,C);w32(0,B+0x205,P);w32(0,B+0x34,M);
    w32(0,C+0x14,Q);w16(0,C+0x12,(uint16_t)parts);w32(0,M+4,NODE);w32(0,M+8,FRAMES);w32(0,NODE+0x20,COUNTS);
    w16(0,C+4,0xffff);w16(0,C+8,0xffff);w16(0,C+0x24,0xffff);
    for(unsigned i=0;i<8;++i){uint32_t q=Q+0x1eu*i;w16(0,q+2,0xffff);w16(0,q+4,0xffff);w16(0,q+6,0xffff);w16(0,q+0x1c,0xffff);}
    w32(0,G(0x6cbee4),OBJ+0x1000);w32(0,OBJ+0x1000,40);w32(0,G(0x4a961e),0x4a4a4a4au);w32(0,G(0x4a9584),MODELS);
}
static void parts_done_checks(void){
    es2_weapon_registers r;building(3);
    es2_base_parts_done(&memory,B,&r);CHECK(r.eax==1&&r.edx==3&&r.ecx==3);
    w16(0,Q+0x1e + 6,2);w16(0,Q+0x1e,5);w16(0,P+11,4);
    es2_base_parts_done(&memory,B,&r);CHECK(r.eax==0&&r.edx==1&&r.ecx==3);
    w16(0,P+11,5);es2_base_parts_done(&memory,B,&r);CHECK(r.eax==1&&r.edx==3);
    w16(0,C+0x12,0xfffe);es2_base_parts_done(&memory,B,&r);CHECK(r.eax==0&&r.edx==0&&r.ecx==0xfffffffeu);
    w16(0,C+0x12,0);es2_base_parts_done(&memory,B,&r);CHECK(r.eax==1&&r.edx==0&&r.ecx==0);
}
static void base_update_checks(void){
    es2_weapon_graph g=graph();building(0);
    /* Dead: base_parts' EDX. */
    arena[B-OBJ+0x99]=1;calls=0;uint64_t v=es2_base_update_registers(&memory,&g,B,entry);
    CHECK(v==((uint64_t)0x31<<32)&&calls==1&&log_[0].f==G(0x403914)&&log_[0].a[0]==B&&log_[0].r[0]==1&&log_[0].r[1]==2&&log_[0].r[2]==3);
    /* Alive, no slot, no animation: EDX = C. */
    arena[B-OBJ+0x99]=0;calls=0;v=es2_base_update_registers(&memory,&g,B,entry);
    CHECK(v==((uint64_t)C<<32)&&calls==1);
    /* Animation only: EAX = B+92h, EDX = C, ECX = C. */
    w16(0,C+6,1);calls=0;v=es2_base_update_registers(&memory,&g,B,entry);
    CHECK(calls==2&&log_[1].f==G(0x402604)&&log_[1].a[1]==0x64&&log_[1].r[0]==B+0x92&&log_[1].r[1]==C&&log_[1].r[2]==C&&v==((uint64_t)0x41<<32));
    /* Timer running: EAX = 0 into actor_animate, timer EDX. */
    w16(0,C+0x24,2);timer_eax=0xffff0003u;calls=0;v=es2_base_update_registers(&memory,&g,B,entry);
    CHECK(calls==3&&log_[1].f==G(0x467a48)&&log_[1].a[0]==B+0x1f6&&log_[1].r[0]==B+0x92&&log_[1].r[1]==C&&log_[1].r[2]==B+0x1f6);
    CHECK(log_[2].r[0]==0&&log_[2].r[1]==0x5555aaaau);
    /* Frame step: (f + 1) mod count, re-arm word, EAX = slot, EDX = [M+8]. */
    timer_eax=0xffff0000u;w16(0,C+0x26,0x1234);w16(0,FRAMES+4,6);w16(0,COUNTS+4,7);w16(0,C+6,0);calls=0;
    v=es2_base_update_registers(&memory,&g,B,entry);
    CHECK(calls==2&&r16(0,FRAMES+4)==0&&r16(0,B+0x1f7)==0x1234&&v==((uint64_t)FRAMES<<32));
    w16(0,FRAMES+4,0xffff);w16(0,COUNTS+4,0xfffe);w16(0,C+6,1);calls=0;v=es2_base_update_registers(&memory,&g,B,entry);
    CHECK(r16(0,FRAMES+4)==2&&log_[2].r[0]==2&&log_[2].r[1]==FRAMES&&log_[2].r[2]==C);
}
static void stage0_checks(void){
    es2_weapon_graph g=graph();building(1);
    /* Sink: [C+4] = -1, [Q+2] = -1, one part. */
    w16(0,P+5,1);timer_eax=0;calls=0;uint64_t v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(v==((uint64_t)C<<32|0xcafef00du)&&calls==2&&log_[0].r[0]==G(0x497485)&&log_[0].r[1]==2&&log_[0].r[2]==3);
    CHECK(log_[1].f==G(0x467a48)&&log_[1].a[0]==P+2&&log_[1].r[2]==P+2&&log_[1].r[1]==C);
    CHECK(arena[B-OBJ+0x38]==1&&r32(0,B+0x2e)==0xfffe7960u&&r16(0,P+5)==0);
    /* Part FIRE: object without fire entry, part with one. */
    building(2);w16(0,P+5,1);w16(0,C+4,3);w16(0,Q+6,7);fire_result=OBJ+0x900;calls=0;
    v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(calls==4&&log_[2].f==G(0x46b430)&&log_[2].r[0]==C&&log_[2].r[1]==C&&log_[2].r[2]==P+2);
    CHECK(log_[3].f==G(0x46b48c)&&log_[3].a[0]==OBJ+0x900&&log_[3].a[1]==B&&log_[3].a[2]==0xffffffffu&&log_[3].a[3]==Q+0xa&&log_[3].a[4]==0xabcd0007u);
    CHECK(log_[3].r[0]==OBJ+0x900&&log_[3].r[1]==0xabcd0007u&&log_[3].r[2]==Q+0xa&&r32(0,OBJ+0x1000)==36&&r16(0,F(0x1c))==0x20);
    /* Object FIRE: all fire parts destroyed. */
    building(2);w16(0,P+5,1);w16(0,C+4,3);w16(0,C+8,9);w16(0,Q+6,7);w16(0,Q,5);w16(0,P,5);calls=0;
    v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(calls==4&&log_[2].r[0]==1&&log_[2].r[1]==2&&log_[2].r[2]==B);
    CHECK(log_[3].a[3]==C+0xa&&log_[3].a[4]==9&&log_[3].r[0]==C+0xa&&log_[3].r[1]==OBJ+0x900&&log_[3].r[2]==9&&r16(0,F(0x1c))==8);
    /* Not all destroyed: no FIRE. */
    w16(0,P+5,1);w16(0,P,4);calls=0;v=es2_base_parts_registers(&memory,&g,B,entry);CHECK(calls==2);
    /* Allocation fails. */
    w16(0,P+5,1);w16(0,P,5);fire_result=0;calls=0;v=es2_base_parts_registers(&memory,&g,B,entry);CHECK(calls==3);
}
static void stage1_checks(void){
    es2_weapon_graph g=graph();building(2);
    /* Part blast with kind Q+4, debris, frame and chain. */
    w16(0,P+11+5,2);w16(0,Q+0x1e + 4,1);w16(0,Q+0x1e + 2,3);timer_eax=0;calls=0;
    uint64_t v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(calls==5&&log_[2].f==G(0x4036c0)&&log_[2].a[0]==B&&log_[2].a[1]==P+11&&log_[2].a[2]==1&&log_[2].a[3]==Q+0x1e + 0xa);
    CHECK(log_[2].r[0]==((C&0xffff0000u)|1)&&log_[2].r[1]==Q+0x1e + 0xa&&log_[2].r[2]==1);
    CHECK(log_[3].f==G(0x40379c)&&log_[3].a[1]==Q+0x1e&&log_[3].r[0]==0xb4u&&log_[3].r[1]==0xc4u);
    CHECK(log_[4].f==G(0x403890)&&log_[4].a[1]==1&&log_[4].r[0]==FRAMES&&log_[4].r[1]==1&&log_[4].r[2]==M&&r16(0,FRAMES+6)==1);
    CHECK(v==((uint64_t)C<<32|0xcafef00du));
    /* Object blast and the destroyed model. */
    building(1);w16(0,P+5,2);w16(0,C+8,4);w16(0,C+0x10,0xfffe);w16(0,Q+6,1);w16(0,C+4,2);w32(0,MODELS+8,0x7777u);calls=0;
    v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(calls==4&&log_[2].a[2]==0xfffffffeu&&log_[2].a[3]==C+0xa&&log_[2].r[0]==C&&log_[2].r[1]==C+0xa&&r32(0,M+4)==0x7777u);
    /* No blast kind: debris gets EAX = C high | FFFFh, EDX = the scan index. */
    building(1);w16(0,P+5,2);calls=0;v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(calls==3&&log_[2].f==G(0x40379c)&&log_[2].r[0]==(C&0xffff0000u|0xffff)&&log_[2].r[1]==1&&log_[2].r[2]==B);
}
static void puff_checks(void){
    es2_weapon_graph g=graph();building(1);
    w16(0,P+5,4);globals2[3]=1;w16(0,Q+0x16,3);w16(0,Q+0x18,0x8001);w16(0,Q+0x1a,5);w16(0,Q+0x10,100);w16(0,Q+0x12,0xff00);w16(0,Q+0x14,7);
    w16(0,Q+4,1);tables[0x24+2]=0x0f;rng_value=2;alloc_result=OBJ+0xa00;timer_eax=0;calls=0;
    uint64_t v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(calls==9&&log_[2].f==G(0x492f24)&&log_[2].a[1]==(((P+2)&0xffff0000u)|3)*2u&&log_[2].r[0]==1&&log_[2].r[1]==0x5555aaaau);
    CHECK(log_[3].a[1]==(0x12340000u|0x8001)*2u&&log_[3].r[0]==0xffffffffu&&log_[3].r[1]==3);
    CHECK(log_[5].f==G(0x47ebb8)&&log_[5].a[0]==B+0xc&&log_[5].a[1]==B+0x12&&log_[5].r[0]==B+0xc&&log_[5].r[2]==B+0xc&&r16(0,B+0x32)==1);
    CHECK(log_[6].f==G(0x48043c)&&log_[6].a[0]==F(0x64)&&log_[6].a[1]==B+0x12&&log_[6].a[2]==F(0x58));
    CHECK(r32(0,F(0x64))==(uint32_t)(2-3+100)&&r32(0,F(0x60))==(uint32_t)(2+0x7fff-0x100)&&r32(0,F(0x5c))==(uint32_t)(2-5+7));
    CHECK(log_[7].f==G(0x471b30)&&log_[7].a[0]==0x4a4a4a4au&&log_[8].f==G(0x407f1c)&&log_[8].a[1]==((OBJ+0xa00)&0xffff0000u|0x0f));
    CHECK(log_[8].a[2]==F(0x58)&&log_[8].a[3]==B&&log_[8].a[4]==1&&log_[8].r[1]==OBJ+0xa00&&log_[8].r[2]==1&&r16(0,P+3)==300&&r16(0,F(0x1c))==0x38);
    CHECK(r16(0,P+5)==3&&v==((uint64_t)C<<32|0xcafef00du));
    /* Even stage with detail 1: no puff, no re-arm; stage 4 chains. */
    w16(0,P+5,5);w16(0,P+3,0);calls=0;v=es2_base_parts_registers(&memory,&g,B,entry);
    CHECK(calls==3&&log_[2].f==G(0x403890)&&log_[2].r[0]==0&&log_[2].r[2]==P+2&&r16(0,P+3)==0);
    globals2[3]=0;w16(0,P+5,3);calls=0;v=es2_base_parts_registers(&memory,&g,B,entry);CHECK(calls==2);
}
static void chain_checks(void){
    es2_weapon_graph g=graph();building(4);
    /* Parts 1 and 3 depend on part 0; part 2 depends on part 1. */
    w16(0,Q+0x1e*1+0x1c,0);w16(0,Q+0x1e*3+0x1c,0);w16(0,Q+0x1e*2+0x1c,1);
    for(unsigned i=0;i<4;++i)w16(0,Q+0x1eu*i,(uint16_t)(10+i));
    w16(0,P+11*3,13);w32(0,P+7,0xdeadbeefu);calls=0;
    uint64_t v=es2_base_part_chain_registers(&memory,&g,B,0,entry);
    CHECK(calls==2&&log_[0].f==SLOT74&&log_[0].a[0]==B&&log_[0].a[1]==1&&log_[0].a[2]==11&&log_[0].a[3]==0xdeadbeefu);
    CHECK(log_[0].r[0]==VTABLE&&log_[0].r[1]==P&&log_[0].r[2]==0xdeadbeefu&&log_[1].f==G(0x403890)&&log_[1].a[1]==1&&log_[1].r[0]==0xa1u);
    CHECK(v==((uint64_t)C<<32|13));
    /* No dependent: EAX = the last parent word (part 3's, 0); no part: R. */
    calls=0;v=es2_base_part_chain_registers(&memory,&g,B,7,entry);CHECK(calls==0&&v==((uint64_t)C<<32));
    w16(0,C+0x12,0);v=es2_base_part_chain_registers(&memory,&g,B,7,entry);CHECK(calls==0&&v==((uint64_t)C<<32|(P+77)));
}
static void blast_checks(void){
    es2_weapon_graph g=graph();building(1);
    /* Kind 0: at the position, explosion type 16, re-arm 2000. */
    memcpy(tables+0xc,"\x10\x00\x15\x00\x0f\x00\x0f\x00\xd0\x07\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00",24);
    w32(0,B+0x26,1);w32(0,B+0x2a,2);w32(0,B+0x2e,3);alloc_result=0x00120034u;calls=0;
    uint64_t v=es2_base_part_blast_registers(&memory,&g,B,P,0,Q+0xa,entry);
    CHECK(calls==3&&log_[0].r[0]==G(0x497435)&&log_[1].f==G(0x471b30)&&log_[1].r[0]==0x4a4a4a4au&&log_[1].r[1]==3&&log_[1].r[2]==1);
    CHECK(log_[2].f==G(0x407f1c)&&log_[2].a[0]==0x00120034u&&log_[2].a[1]==0x4a4a0010u&&log_[2].a[2]==F(0x38)&&log_[2].r[1]==F(0x38));
    CHECK(r32(0,F(0x38))==1&&r32(0,F(0x34))==2&&r32(0,F(0x30))==3&&r16(0,P+3)==2000&&v==((uint64_t)P<<32|(0xb5u&0xffff0000u)|2000));
    /* Kind 1: transform after the lazy matrix; allocation fails. */
    alloc_result=0;calls=0;v=es2_base_part_blast_registers(&memory,&g,B,P,1,Q+0xa,entry);
    CHECK(calls==4&&log_[1].f==G(0x47ebb8)&&log_[1].r[0]==B+0x12&&log_[1].r[1]==B+0xc&&log_[1].r[2]==3);
    CHECK(log_[2].f==G(0x4801d4)&&log_[2].a[0]==Q+0xa&&log_[2].r[1]==Q+0xa&&log_[2].r[2]==F(0x38)&&r16(0,P+3)==0&&v==((uint64_t)P<<32));
    CHECK(r32(0,F(0x2c))==B+0xc&&r32(0,F(0x28))==21);
}
static void debris_checks(void){
    es2_weapon_graph g=graph();building(1);
    w16(0,Q+8,6);w32(0,G(0x4a9650),OBJ+0xb00);w32(0,G(0x4a9656),0x66u);calls=0;
    uint64_t v=es2_base_part_debris_registers(&memory,&g,B,Q,entry);
    CHECK(calls==8&&log_[0].f==G(0x47ebb8)&&log_[0].r[0]==B+0x12&&log_[0].r[1]==2&&log_[0].r[2]==3);
    CHECK(log_[1].f==G(0x4801d4)&&log_[1].a[0]==Q+0xa&&log_[1].a[2]==F(0x14)&&r32(0,G(0x4a964c))==G(0x4a95c0));
    CHECK(log_[2].f==G(0x4089bc)&&log_[2].a[0]==F(2)&&log_[2].r[0]==((F(0x14)&0xffff0000u)|6)&&log_[2].r[1]==F(2));
    CHECK(log_[3].f==G(0x408530)&&log_[3].a[0]==OBJ+0xb00+60&&log_[3].a[1]==0x66u&&log_[3].a[2]==F(0x14)&&log_[3].a[3]==0);
    CHECK(log_[3].r[0]==OBJ+0xb00+60&&log_[3].r[1]==60&&log_[3].r[2]==6&&log_[4].r[0]==F(4)&&log_[4].r[1]==0x92u&&log_[4].r[2]==0x93u);
    CHECK(log_[5].a[0]==OBJ+0xb00+100&&log_[7].a[0]==OBJ+0xb00+120&&v==((uint64_t)0x92<<32|0x91));
    w16(0,Q+8,5);calls=0;v=es2_base_part_debris_registers(&memory,&g,B,Q,entry);CHECK(calls==3);
}
int main(void){
    parts_done_checks();base_update_checks();stage0_checks();stage1_checks();puff_checks();chain_checks();
    blast_checks();debris_checks();
    printf("PASS: base_update %u checks\n",checks);return 0;
}
