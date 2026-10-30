#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/mech_part_hit.h"
/* Synthetic guest: the MECH, its vtable, definition, configuration,
 * weapon system, packet, effect table and point at 90000000h, an image
 * base I with the globals 0x4a961e, 0x4a9e6a/0x4a9e6c and 0x6cbee4, a
 * frame scratch at A0000000h. Every game call is logged with its
 * arguments and entry registers; the stubs return planned values. The
 * expectations are read from the reference listing (hit_tests.md,
 * "MECH part hit"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_MECH_PART_HIT_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_MECH_PART_HIT_SCRATCH],globals1[0x880],globals2[0x10];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mech_part_hit line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4a9600)<sizeof(globals1))return globals1+(a-G(0x4a9600));
    if(a-G(0x6cbee0)<sizeof(globals2))return globals2+(a-G(0x6cbee0));
    fprintf(stderr,"mech_part_hit: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
#define A (OBJ+0x100)
#define VT (OBJ+0x3c0)
#define DEF (OBJ+0x440)
#define C (OBJ+0x460)
#define P (OBJ+0x4c0)
#define FX (OBJ+0x4e0)
#define PT (OBJ+0x500)
#define FLAGS (OBJ+0x520)
#define SYS (OBJ+0x5c0)
#define SLOTS (OBJ+0x5e0)
#define WP(i) (OBJ+0x600+0x20u*(i))
#define WD(i) (OBJ+0x680+0x20u*(i))
#define LIST (OBJ+0x840)
#define COUNTER (OBJ+0x980)
#define REC (OBJ+0x9a0)
#define O (OBJ+0xa00)
#define S74 UINT32_C(0x50000074)
#define S70 UINT32_C(0x50000070)
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; } logged;
static logged log_[32];static unsigned calls;
static uint32_t ratios[2],rngs[2],alloc_result,debris_word;static unsigned ratio_index,rng_index;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<32);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49630c)){CHECK(n==0&&r->eax==G(0x49a108));w32(0,F(0x24),0xcafef00du);r->eax=0x1234u;r->edx=0x2345u;r->ecx=0xabcd4321u;}
    else if(f==G(0x47e0b0)){CHECK(n==2);r->eax=(uint32_t)(((int64_t)(int32_t)a[0]*(int32_t)a[1])>>10);r->edx=0x7e07e0u;r->ecx=0x7e1u;}
    else if(f==G(0x40dbc0)){CHECK(n==2&&a[0]==A+0x206);r->eax=0x55aa0000u|ratios[ratio_index++&1];r->edx=0xdb01u;r->ecx=0xdb02u;}
    else if(f==S74){CHECK(n==4);r->eax=0x7401u;r->edx=0x7402u;r->ecx=0x7403u;}
    else if(f==S70){CHECK(n==5);r->eax=0x7001u;r->edx=0x7002u;r->ecx=0x7003u;}
    else if(f==G(0x434010)){CHECK(n==1&&a[0]==0);r->eax=0x3401u;r->edx=0x3402u;r->ecx=0x3403u;}
    else if(f==G(0x420a68)){CHECK(n==1);r->eax=0x0a61u;r->edx=0x0a62u;r->ecx=0x0a63u;}
    else if(f==G(0x492ee0)){CHECK(n==1&&a[0]==G(0x4d2599));r->eax=rngs[rng_index++&1];r->edx=0x2ee1u;r->ecx=0x2ee2u;}
    else if(f==G(0x40f57c)){CHECK(n==3);r->eax=0xf571u;r->edx=0xf572u;r->ecx=0xf573u;}
    else if(f==G(0x40da38)){CHECK(n==3);r->eax=0xda31u;r->edx=0xda32u;r->ecx=0xda33u;}
    else if(f==G(0x471b30)){CHECK(n==1);r->eax=alloc_result;r->edx=0x71u;r->ecx=0x72u;}
    else if(f==G(0x407f1c)){CHECK(n==5);r->eax=0x7f11u;r->edx=0x7f12u;r->ecx=0x7f13u;}
    else if(f==G(0x4089bc)){CHECK(n==1);debris_word=r16(0,a[0]);w16(0,a[0],(uint16_t)(debris_word-1));r->eax=REC;r->edx=0x89b2u;r->ecx=0x89b3u;}
    else if(f==G(0x408530)){CHECK(n==4);r->eax=0x8531u;r->edx=0x8532u;r->ecx=0x8533u;}
    else {fprintf(stderr,"mech_part_hit: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0,sizeof(arena));memset(scratch,0x5a,sizeof(scratch));memset(globals1,0,sizeof(globals1));memset(globals2,0,sizeof(globals2));
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,40);calls=0;ratio_index=0;rng_index=0;alloc_result=O;
    w32(0,A,VT);w32(0,VT+0x74,S74);w32(0,VT+0x70,S70);w32(0,A+0x45,DEF);w32(0,A+0x1f2,C);w32(0,A+0x202,SYS);w32(0,A+0x20e,FLAGS);
    w16(0,P+4,0x0120);w16(0,P+8,0x0400);w32(0,P+0xa,FX);w32(0,P+0xe,0x0ee0ee0eu);
    for(unsigned i=0;i<12;++i)w16(0,FX+2*i,(uint16_t)(0x100+i));
    for(unsigned i=0;i<64;++i)w16(0,FLAGS+2*i,0xffff);
    w32(0,G(0x4a961e),0x61e61e61u);w32(0,REC,OBJ+0xc00);w32(0,REC+6,OBJ+0xd00);
    /* Weapons: slot 0 empty, slot 1 kind 1, slot 2 kind 2; [W+1Ch]+56h
     * holds 0x9900+i. */
    w32(0,SYS+4,SLOTS);w16(0,SYS+8,3);
    for(unsigned i=1;i<3;++i){w32(0,SLOTS+4*i,WP(i));w32(0,WP(i)+0x18,WD(i));arena[WD(i)-OBJ+0x17]=(uint8_t)i;
        w32(0,WP(i)+0x1c,OBJ+0x780+4*i-0x56);w16(0,OBJ+0x780+4*i,(uint16_t)(0x9900+i));}
    w32(0,G(0x4a9e6c),LIST);w16(0,G(0x4a9e6a),3);
}
/* The player's part 1 drops from bracket 6 to 1: slot +74h, slot +70h,
 * panel jolt and message 12h, an explosion of words 8..11 and debris. */
static void player_checks(void){
    es2_weapon_graph g=graph();reset();arena[A-OBJ+0xa3]=1;w16(0,C+0x56,1);
    ratios[0]=200;ratios[1]=50;rngs[0]=0x12345677u;
    uint64_t v=es2_mech_part_hit_registers(&memory,&g,A,1,P,PT,entry);
    CHECK(calls==13);
    CHECK(log_[0].f==G(0x49630c)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    /* splash = (0x400 * 0x120) >> 10 = 0x120 */
    CHECK(log_[1].f==G(0x47e0b0)&&log_[1].a[0]==0x400&&log_[1].a[1]==0x120&&log_[1].r[0]==0x120&&log_[1].r[1]==0x120&&log_[1].r[2]==0xabcd0120u);
    CHECK(log_[2].f==G(0x40dbc0)&&log_[2].a[1]==1&&log_[2].r[0]==0x120&&log_[2].r[1]==0x7e07e0u&&log_[2].r[2]==A+0x206);
    /* damage - splash = 0; EAX's high word is the sign of 200 >> 5 */
    CHECK(log_[3].f==S74&&log_[3].a[0]==A&&log_[3].a[1]==1&&log_[3].a[2]==0&&log_[3].a[3]==0x0ee0ee0eu);
    CHECK(log_[3].r[0]==0&&log_[3].r[1]==VT&&log_[3].r[2]==0x0ee0ee0eu&&r16(0,F(0x28))==6&&r16(0,F(0x32))==200);
    CHECK(log_[4].f==S70&&log_[4].a[1]==0x120&&log_[4].a[2]==PT&&log_[4].a[3]==500&&log_[4].a[4]==0x0ee0ee0eu);
    CHECK(log_[4].r[0]==0x0ee0ee0eu&&log_[4].r[1]==PT&&log_[4].r[2]==VT);
    CHECK(log_[5].f==G(0x40dbc0)&&log_[5].r[0]==A+0x206&&log_[5].r[1]==0x7002u&&log_[5].r[2]==0x7003u);
    CHECK(log_[6].f==G(0x434010)&&log_[6].r[0]==A+0x92&&log_[6].r[1]==0&&log_[6].r[2]==0xdb02u);
    CHECK(log_[7].f==G(0x420a68)&&log_[7].a[0]==0x12&&log_[7].r[0]==0x3401u&&log_[7].r[1]==0x3402u&&log_[7].r[2]==0);
    /* f-38h = 0, C+56h set, but part 1 < 19: no weapon. */
    CHECK(log_[8].f==G(0x471b30)&&log_[8].a[0]==0x61e61e61u&&log_[8].r[0]==0x61e61e61u&&log_[8].r[1]==C&&log_[8].r[2]==0x12);
    CHECK(log_[9].f==G(0x492ee0)&&log_[9].r[0]==O&&log_[9].r[1]==PT&&log_[9].r[2]==0x61e61e61u&&r16(0,F(0x14))==8);
    /* type = word 4*2 + (0x7677 & 3) = 8 + 3 */
    CHECK(log_[10].f==G(0x407f1c)&&log_[10].a[0]==O&&log_[10].a[1]==((P&0xffff0000u)|0x10b)&&log_[10].a[2]==PT&&log_[10].a[3]==A&&log_[10].a[4]==1);
    CHECK(log_[10].r[0]==FX&&log_[10].r[1]==log_[10].a[1]&&log_[10].r[2]==O&&r32(0,COUNTER)==36);
    CHECK(log_[11].f==G(0x4089bc)&&log_[11].a[0]==F(0x4a)&&debris_word==2&&log_[11].r[0]==((COUNTER&0xffff0000u)|2)&&log_[11].r[1]==F(0x4a)&&log_[11].r[2]==0x7f13u);
    /* The word is reread after 4089bc (lowered to 1). */
    CHECK(log_[12].f==G(0x408530)&&log_[12].a[0]==OBJ+0xc00+10&&log_[12].a[1]==OBJ+0xd00&&log_[12].a[2]==PT&&log_[12].a[3]==0);
    CHECK(log_[12].r[0]==OBJ+0xc0a&&log_[12].r[1]==1&&log_[12].r[2]==10);
    CHECK(v==((uint64_t)0x8532u<<32|0xcafef00du));
}
/* A weapon-mount part 21 of a side-1 MECH drops from 100h to 60h: the
 * chance 409 < 410 destroys weapon kind 2, the results list gets the item,
 * the region is damaged; the pool is empty; debris. */
static void weapon_checks(void){
    es2_weapon_graph g=graph();reset();w16(0,C+0x56,7);arena[DEF-OBJ+0x12]=1;w16(0,P+8,0);
    ratios[0]=0x100;ratios[1]=0x60;rngs[0]=0xffff0199u;alloc_result=0;
    uint64_t v=es2_mech_part_hit_registers(&memory,&g,A,21,P,PT,entry);
    /* DI = 0: no slot +70h, no panel (not the player). */
    CHECK(calls==12&&log_[3].f==S74&&log_[3].a[2]==0x120&&log_[4].f==G(0x40dbc0));
    CHECK(log_[5].f==G(0x492ee0)&&log_[5].r[0]==((A+0x92)&0xffff0000u)+10&&log_[5].r[1]==C&&log_[5].r[2]==DEF&&r16(0,F(0x3c))==10);
    /* weapon_by_kind(SYS, 2) = WP(2) at index 2: EAX = W, EDX = 2, ECX = W */
    CHECK(log_[6].f==G(0x40f57c)&&log_[6].a[0]==WP(2)&&log_[6].a[1]==A&&log_[6].a[2]==1);
    CHECK(log_[6].r[0]==WP(2)&&log_[6].r[1]==2&&log_[6].r[2]==WP(2)&&r16(0,FLAGS+42)==0&&r16(0,FLAGS+40)==0xffff);
    /* (500 * 96) >> 10 = 46; (256 - 46) * 100 >> 8 = 82 */
    CHECK(log_[7].f==G(0x47e0b0)&&log_[7].a[0]==500&&log_[7].a[1]==0x60&&log_[7].r[0]==0x60&&log_[7].r[1]==1&&log_[7].r[2]==0x60);
    CHECK(r32(0,LIST+12)==((uint32_t)82<<16|0x9902)&&r16(0,G(0x4a9e6a))==4);
    CHECK(log_[8].f==G(0x40da38)&&log_[8].a[0]==A+0x206&&log_[8].a[1]==21&&log_[8].a[2]==10000);
    CHECK(log_[8].r[0]==3&&log_[8].r[1]==LIST&&log_[8].r[2]==A+0x206);
    CHECK(log_[9].f==G(0x471b30)&&log_[9].r[1]==0xda32u&&log_[9].r[2]==0xda33u);
    CHECK(log_[10].f==G(0x4089bc)&&log_[10].r[0]==2&&log_[10].r[2]==0x61e61e61u&&v==((uint64_t)0x8532u<<32|0xcafef00du));
    /* At the threshold (410) nothing is destroyed; with side byte 0 the
     * threshold is 123. */
    reset();w16(0,C+0x56,7);arena[DEF-OBJ+0x12]=1;w16(0,P+8,0);ratios[0]=0x100;ratios[1]=0x60;rngs[0]=0x0000f19au;alloc_result=0;
    es2_mech_part_hit_registers(&memory,&g,A,21,P,PT,entry);
    CHECK(calls==9&&log_[5].f==G(0x492ee0)&&log_[6].f==G(0x471b30)&&log_[6].r[1]==0&&log_[6].r[2]==10);
    reset();w16(0,C+0x56,7);w16(0,P+8,0);ratios[0]=0x100;ratios[1]=0x60;rngs[0]=122;alloc_result=0;
    es2_mech_part_hit_registers(&memory,&g,A,21,P,PT,entry);
    /* side byte 0: no item; 40da38 gets EAX = [A+45h], EDX = 0 */
    CHECK(log_[6].f==G(0x40f57c)&&log_[7].f==G(0x40da38)&&log_[7].r[0]==DEF&&log_[7].r[1]==0&&r16(0,G(0x4a9e6a))==3);
}
/* Unchanged bracket: no extras, explosion of words 4..7, no debris; a
 * bracket change to exactly 100h: no weapon and no debris. */
static void bracket_checks(void){
    es2_weapon_graph g=graph();reset();w16(0,C+0x56,7);ratios[0]=0x40;ratios[1]=0x5f;rngs[0]=0x0000fffeu;
    uint64_t v=es2_mech_part_hit_registers(&memory,&g,A,25,P,PT,entry);
    CHECK(calls==9&&log_[7].f==G(0x492ee0)&&log_[8].f==G(0x407f1c)&&log_[8].a[1]==((P&0xffff0000u)|0x106));
    CHECK(r16(0,F(0x2e))==1&&r16(0,F(0x30))==1&&r32(0,F(0x38))==0&&v==((uint64_t)0x7f12u<<32|0xcafef00du));
    reset();arena[A-OBJ+0xa3]=1;w16(0,C+0x56,7);ratios[0]=0x80;ratios[1]=0x100;rngs[0]=0;
    v=es2_mech_part_hit_registers(&memory,&g,A,0,P,PT,entry);
    CHECK(calls==9&&log_[6].f==G(0x471b30)&&log_[6].r[1]==1&&log_[8].a[1]==((P&0xffff0000u)|0x108)&&v==((uint64_t)0x7f12u<<32|0xcafef00du));
}
static void leaf_checks(void){
    es2_weapon_registers r={0,0,0x77};reset();
    es2_weapon_by_kind(&memory,SYS,0xffff0001u,&r);CHECK(r.eax==WP(1)&&r.edx==1&&r.ecx==WP(1));
    r.ecx=0x77;es2_weapon_by_kind(&memory,SYS,3,&r);CHECK(r.eax==0&&r.edx==3&&r.ecx==WP(2));
    /* A sign-extended kind byte matches the word FFFFh. */
    arena[WD(2)-OBJ+0x17]=0xff;es2_weapon_by_kind(&memory,SYS,0xffff,&r);CHECK(r.eax==WP(2)&&r.edx==2);
    w16(0,SYS+8,0x8000);r.ecx=0x77;es2_weapon_by_kind(&memory,SYS,1,&r);CHECK(r.eax==0&&r.edx==0&&r.ecx==0x77);
    w32(0,OBJ+0x900,0x11223344u);w16(0,G(0x4a9e6a),0xfffe);es2_results_append(&memory,I,OBJ+0x900,&r);
    CHECK(r.eax==0xfffffffeu&&r.edx==LIST&&r.ecx==0x11223344u&&r32(0,LIST-8)==0x11223344u&&r16(0,G(0x4a9e6a))==0xffff);
}
int main(void){
    player_checks();weapon_checks();bracket_checks();leaf_checks();
    printf("PASS: mech_part_hit %u checks\n",checks);return 0;
}
