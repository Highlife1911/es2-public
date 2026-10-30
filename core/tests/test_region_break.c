#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/region_damage.h"
/* region_hit 0x40da38 and region_break 0x40d434 on a synthetic guest:
 * data at 90000000h, the adapter scratch at A0000000h, image base B (the
 * globals 498864h..4ab000h and 6cbee4h are mapped). Every game call is
 * logged with its arguments and entry registers and returns planned
 * registers; the callees' effects that the functions read back (region
 * find's word, the record head, the transform output, debris table words,
 * the stack pushes of region_break) are modelled in the stub. */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define HEAD UINT32_C(0x0badf00d)
static uint8_t arena[4096],scratch[ES2_REGION_BREAK_SCRATCH],image[0x13000],counter_cell[8];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"region_break line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x498000)<sizeof(image))return image+(a-G(0x498000));
    if(a-G(0x6cbee4)<sizeof(counter_cell))return counter_cell+(a-G(0x6cbee4));
    fprintf(stderr,"region_break: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
/* Layout: D +0 V, +4 part words, +8 intact words, +0Ch C, +10h S, +14h A,
 * +18h count; C: [C] records (18 bytes), [C+4] limits; A +0Ch.. pose, +32h
 * flag, +34h H; H +4 L (vtable at L), +8 state words, +16h node poses; Q
 * the stack {list, count byte +5}. */
enum { D=0x000, V=0x040, WORDS=0x080, INTACT=0x0c0, C=0x100, RECS=0x120, LIMITS=0x1a0, S=0x1c0, A=0x200, H=0x280,
    L=0x2c0, VT=0x2e0, STATE=0x320, NODES=0x360, Q=0x400, LIST=0x410, E=0x440, NODE=0x480 };
#define AT(x) (OBJ+(x))
#define F (FRAME+ES2_REGION_BREAK_FRAME)
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; uint16_t word; } logged;
static logged log_[48];static unsigned calls,observed;
static uint32_t ret[48][3];
/* region_break stub effect: push these indices when called (cascade). */
static uint16_t pushes[48][2];static unsigned npushes[48];
static uint16_t found_word=0x0003;static int find_writes=1;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<48);logged *l=log_+calls;memset(l,0,sizeof(*l));l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49630c)){w32(0,F-0x34u,HEAD);r->eax=0x11111111u;++calls;return;} /* EDX/ECX unchanged */
    if(f==G(0x40ca8c)&&find_writes)w16(0,a[2],found_word);
    if(f==G(0x40d3ec)){l->word=r16(0,a[2]);if(ret[calls][2]>>16)w16(0,a[2],(uint16_t)(ret[calls][2]>>16));}
    if(f==G(0x4801d4)){l->word=r8(a[1]+18);for(unsigned i=0;i<3;++i)w32(0,a[2]+4*i,0x1000u*(i+1));}
    if(f==G(0x4089bc))l->word=r16(0,a[0]);
    if(f==G(0x40d434))for(unsigned j=0;j<npushes[calls];++j){
        uint8_t k=r8(AT(Q+5));w16(0,AT(LIST)+2u*k,pushes[calls][j]);w8(AT(Q+5),(uint8_t)(k+1));}
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2]&0xffffu;++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){
    calls=observed=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));
    memset(image,0,sizeof(image));memset(npushes,0,sizeof(npushes));found_word=3;find_writes=1;
    w32(0,AT(D),AT(V));w32(0,AT(D+4),AT(WORDS));w32(0,AT(D+8),AT(INTACT));w32(0,AT(D+0xc),AT(C));w32(0,AT(D+0x10),AT(S));
    w32(0,AT(D+0x14),AT(A));w16(0,AT(D+0x18),4);w32(0,AT(C),AT(RECS));w32(0,AT(C+4),AT(LIMITS));
    w32(0,AT(A+0x34),AT(H));w32(0,AT(H+4),AT(L));w32(0,AT(L),AT(VT));w32(0,AT(VT+0x20),0x77770000u);
    w32(0,AT(H+8),AT(STATE));w32(0,AT(H+0x16),AT(NODES));
    w32(0,G(0x498864),AT(Q));w32(0,AT(Q),AT(LIST));w8(AT(Q+5),0);
    w32(0,G(0x4a961e),0xcafe0000u);w32(0,G(0x6cbee4),G(0x6cbee4)+4);w32(0,G(0x6cbee4)+4,100);
    for(unsigned i=0;i<4;++i)w16(0,AT(INTACT)+2*i,1);
}
static uint32_t rec(unsigned part){return AT(RECS)+18u*part;}

static void test_hit(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    /* Absorbed: 40d3ec returns AX = 0 with a high word; the pair keeps it. */
    reset();w16(0,rec(1),0x1234);
    ret[0][0]=0x50000u;ret[0][1]=0xd1d1;ret[0][2]=0xe1e1;
    uint64_t r=es2_region_hit_registers(&memory,&g,AT(D),0xabcd0001u,0x99990040u,entry,&ecx);
    CHECK(calls==1&&observed==1&&(uint32_t)r==0x50000u&&r>>32==0xd1d1&&ecx==0xe1e1);
    CHECK(log_[0].f==G(0x40d3ec)&&log_[0].n==3&&log_[0].a[0]==AT(WORDS)+2&&log_[0].a[1]==0x1234u&&log_[0].a[2]==FRAME);
    CHECK(log_[0].r[0]==0x1234u&&log_[0].r[1]==AT(WORDS)+2&&log_[0].r[2]==FRAME&&log_[0].word==0x40);
    /* Negative part index: the pointers go below the arrays. */
    reset();w16(0,AT(RECS)-18u,0x55);
    r=es2_region_hit_registers(&memory,&g,AT(D),0xffffu,7,entry,&ecx);
    CHECK(log_[0].a[0]==AT(WORDS)-2u&&log_[0].a[1]==0xffff0055u);
    /* Broken, the overflow 0x30 spreads; no live region left: ECX =
     * s8(R+3)+1. */
    reset();arena[RECS+18+3]=0xfe;
    ret[0][0]=1;ret[0][2]=0x00300000u;ret[1][0]=0x70000u;ret[1][1]=0x4242;
    r=es2_region_hit_registers(&memory,&g,AT(D),1,0x20,entry,&ecx);
    CHECK(calls==2&&log_[1].f==G(0x40cf44)&&log_[1].n==4&&log_[1].a[0]==rec(1)&&log_[1].a[1]==AT(V)&&
        log_[1].a[2]==((AT(C)&0xffff0000u)|0x30u)&&log_[1].a[3]==AT(LIMITS));
    CHECK(log_[1].r[0]==18u&&log_[1].r[1]==rec(1)&&log_[1].r[2]==1u);
    CHECK((uint32_t)r==0x70000u&&r>>32==0x4242&&ecx==0xffffffffu);
    /* Spread leaves no region but bit 0 is clear: its ECX. */
    reset();ret[0][0]=1;ret[1][0]=1;ret[1][1]=0x77;ret[1][2]=0x88;
    r=es2_region_hit_registers(&memory,&g,AT(D),2,0x20,entry,&ecx);
    CHECK(calls==2&&(uint32_t)r==0&&r>>32==0x77&&ecx==0x88);
    /* Cascade: the break of part 2 pushes 3 and 1, the break of 1 pushes
     * 0; the stack pops 1, then 0, then 3. */
    reset();arena[RECS+36+5]=1;w16(0,G(0x4a982c),9);
    ret[0][0]=1;ret[1][0]=1;ret[1][1]=0x61;ret[1][2]=0x62;
    npushes[2]=2;pushes[2][0]=3;pushes[2][1]=1;ret[2][2]=0x91;
    npushes[3]=1;pushes[3][0]=0;ret[3][2]=0x92;ret[4][2]=0x93;ret[5][2]=0x94;
    r=es2_region_hit_registers(&memory,&g,AT(D),0x80000002u,0x20,entry,&ecx);
    CHECK(calls==6&&r16(0,G(0x4a982c))==0);
    CHECK(log_[2].f==G(0x40d434)&&log_[2].n==2&&log_[2].a[0]==AT(D)&&log_[2].a[1]==0x80000002u);
    CHECK(log_[2].r[0]==1&&log_[2].r[1]==0x61&&log_[2].r[2]==0x62);
    CHECK(log_[3].a[1]==1&&log_[3].r[0]==AT(LIST)&&log_[3].r[1]==1&&log_[3].r[2]==1);
    CHECK(log_[4].a[1]==0&&log_[4].r[2]==1&&log_[5].a[1]==3&&log_[5].r[2]==0);
    CHECK((uint32_t)r==((AT(Q)&0xffff0000u)|1u)&&r>>32==1&&ecx==0x94&&r8(AT(Q+5))==0);
}

static void test_break(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    /* Already broken: EAX 0, EDX the record head, ECX region_find's. */
    reset();w16(0,AT(INTACT)+2,0);ret[1][0]=AT(E);ret[1][2]=0x4321;
    uint64_t r=es2_region_break_registers(&memory,&g,AT(D),0x12340001u,entry,&ecx);
    CHECK(calls==2&&r==(uint64_t)HEAD<<32&&ecx==0x4321);
    CHECK(log_[0].f==G(0x49630c)&&log_[0].r[0]==G(0x4988e8)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x40ca8c)&&log_[1].a[0]==AT(S)&&log_[1].a[1]==1&&log_[1].a[2]==F-0x36u);
    CHECK(log_[1].r[0]==1&&log_[1].r[1]==AT(S)&&log_[1].r[2]==F-0x36u&&r32(0,F-0x3cu)==AT(E)+8u);
    /* Actor pose, fire kind 0, big explosion, debris word -1, one child. */
    reset();found_word=0xfffe;
    uint32_t R=rec(1);arena[RECS+18+2]=0xff;arena[RECS+18+3]=2;arena[RECS+18+5]=2|4;
    arena[RECS+0+4]=1;arena[RECS+36+4]=1;w16(0,AT(INTACT)+4,0);arena[RECS+54+4]=1;
    for(unsigned i=0;i<20;++i)arena[A+0x12+i]=(uint8_t)(0x40+i);
    w32(0,AT(A+0x26),0x111);w32(0,AT(A+0x2a),0x222);w32(0,AT(A+0x2e),0x333);
    ret[1][0]=AT(E);ret[2][0]=0x0;ret[2][1]=0x5a5a;ret[2][2]=0x6b6b;
    ret[3][0]=0x31;ret[3][1]=0x32;          /* fire_release */
    ret[4][0]=AT(NODE);ret[4][1]=0x42;       /* fire_alloc */
    ret[5][0]=0x51;                          /* fire_create */
    ret[6][0]=0;                             /* euler_matrix (flag 0) */
    ret[7][0]=0x71;                          /* 4801d4 */
    ret[8][0]=0x9000;ret[8][1]=0x81;         /* pool_alloc */
    ret[9][0]=0x91;                          /* explosion_create */
    ret[10][0]=AT(NODE+0x20);                /* debris_table */
    w32(0,AT(NODE+0x20),AT(NODE+0x40));w32(0,AT(NODE+0x26),0xabab);
    r=es2_region_break_registers(&memory,&g,AT(D),1,entry,&ecx);
    CHECK(calls==12&&observed==12);
    CHECK(r16(0,AT(WORDS)+2)==0xffff&&r16(0,AT(INTACT)+2)==0);
    CHECK(log_[2].f==G(0x40cf44)&&log_[2].a[0]==R&&log_[2].a[1]==AT(V)&&log_[2].a[2]==0x7d00u&&log_[2].a[3]==AT(LIMITS));
    CHECK(log_[2].r[0]==R&&log_[2].r[1]==18&&log_[2].r[2]==AT(V));
    CHECK(log_[3].f==G(0x46b62c)&&log_[3].a[0]==AT(A)&&log_[3].r[0]==3&&log_[3].r[1]==0x5a5a&&log_[3].r[2]==AT(A));
    CHECK(log_[4].f==G(0x46b430)&&log_[4].a[0]==0x5b&&log_[4].r[0]==0x31&&log_[4].r[1]==0x32&&log_[4].r[2]==AT(A));
    CHECK(log_[5].f==G(0x46b48c)&&log_[5].n==5&&log_[5].a[0]==AT(NODE)&&log_[5].a[1]==AT(A)&&log_[5].a[2]==0xfffeu&&
        log_[5].a[3]==AT(E)+8&&log_[5].a[4]==0);
    CHECK(log_[5].r[0]==AT(A)&&log_[5].r[1]==AT(NODE)&&log_[5].r[2]==0xfffeu&&r32(0,G(0x6cbee4)+4)==92);
    CHECK(r16(0,AT(STATE)+4)==2);
    CHECK(log_[6].f==G(0x47ebb8)&&log_[6].a[0]==AT(A+0xc)&&log_[6].a[1]==AT(A+0x12)&&log_[6].r[2]==AT(STATE));
    CHECK(r16(0,AT(A+0x32))==1);
    CHECK(log_[7].f==G(0x4801d4)&&log_[7].a[0]==AT(E)+8&&log_[7].a[1]==F-0xc4u&&log_[7].a[2]==F-0x98u&&log_[7].word==0x52);
    CHECK(r32(0,F-0xb0u)==0x1000&&r32(0,F-0xacu)==0x2000&&r32(0,F-0xa8u)==0x3000);
    CHECK(log_[8].f==G(0x471b30)&&log_[8].a[0]==0xcafe0000u&&log_[8].r[0]==0x1000&&log_[8].r[1]==0x2000);
    CHECK(log_[9].f==G(0x407f1c)&&log_[9].a[0]==0x9000&&log_[9].a[1]==0x11&&log_[9].a[2]==F-0xb0u&&log_[9].a[3]==AT(A)&&
        log_[9].a[4]==1&&log_[9].r[2]==F-0xb0u&&r16(0,G(0x4a982c))==1);
    CHECK(log_[10].f==G(0x4089bc)&&log_[10].a[0]==F-0x68u&&log_[10].word==2&&log_[10].r[0]==0&&log_[10].r[2]==G(0x6cbee4)+4);
    CHECK(log_[11].f==G(0x4086bc)&&log_[11].a[0]==AT(NODE+0x40)+20u&&log_[11].a[1]==0xabab&&log_[11].a[2]==F-0xc4u&&
        log_[11].a[3]==0&&log_[11].r[1]==2&&log_[11].r[2]==20);
    /* Children of part 1 that are intact: parts 0 and 3 (2 is broken). */
    CHECK(r8(AT(Q+5))==2&&r16(0,AT(LIST))==0&&r16(0,AT(LIST)+2)==3);
    CHECK((uint32_t)r==1&&r>>32==4&&ecx==HEAD);
    /* Node pose: o = 2, the lookup finds a node with index 1; fire kind 2
     * (bit 1 clear, bit 3 set) with a failed allocation; flag word set
     * (no explosion); the debris word 5. */
    reset();found_word=2;arena[RECS+18+2]=5;arena[RECS+18+3]=0x7f;arena[RECS+18+5]=8;w16(0,AT(A+0x32),1);
    w16(0,G(0x4a982c),0);w16(0,AT(D+0x18),0xffff);
    ret[1][0]=AT(E);ret[2][1]=0x5a;ret[2][2]=0x6b;ret[3][0]=0;ret[3][1]=0x33;
    ret[4][0]=AT(NODE);ret[4][2]=0x4444;w16(0,AT(NODE+4),1);
    ret[5][0]=0x55;ret[6][0]=0x66;ret[7][0]=0;ret[8][0]=AT(NODE+0x20);
    w32(0,AT(NODE+0x20),AT(NODE+0x40));
    r=es2_region_break_registers(&memory,&g,AT(D),1,entry,&ecx);
    CHECK(calls==10);
    CHECK(log_[3].f==G(0x46b430)&&log_[3].r[0]==0x80&&log_[3].r[1]==0x5a&&log_[3].r[2]==0x6b);
    CHECK(r16(0,AT(STATE)+0xfe)==2);
    CHECK(log_[4].f==0x77770000u&&log_[4].a[0]==AT(L)&&log_[4].a[1]==((AT(A)&0xffff0000u)|2u));
    CHECK(log_[4].r[0]==AT(H)&&log_[4].r[1]==AT(L)&&log_[4].r[2]==AT(VT));
    CHECK(log_[5].f==G(0x47fa20)&&log_[5].a[0]==AT(NODES)+32u&&log_[5].a[1]==AT(A+0x12)&&log_[5].a[2]==F-0xc4u);
    CHECK(log_[5].r[0]==AT(NODES)+32u&&log_[5].r[1]==AT(NODES)+32u&&log_[5].r[2]==F-0xc4u);
    CHECK(log_[6].f==G(0x4801d4)&&log_[7].f==G(0x471b30)&&log_[8].f==G(0x4089bc)&&log_[8].word==5&&log_[8].r[0]==5);
    CHECK(log_[9].f==G(0x4086bc)&&log_[9].a[0]==AT(NODE+0x40)+50u&&log_[9].r[1]==50&&log_[9].r[2]==5);
    CHECK((uint32_t)r==1&&r>>32==0xffffffffu&&r8(AT(Q+5))==0);
    /* No node: the identity pose; the lookup's ECX reaches euler_matrix. */
    reset();found_word=0;w16(0,AT(D+0x18),0);arena[RECS+18+3]=0x80;
    ret[1][0]=AT(E);ret[2][2]=0x6b;ret[3][0]=0;ret[3][2]=0x3c3c;ret[7][0]=AT(NODE+0x20);
    w16(0,G(0x4a982c),1);
    r=es2_region_break_registers(&memory,&g,AT(D),1,entry,&ecx);
    CHECK(calls==9&&log_[3].f==0x77770000u&&log_[3].r[2]==AT(VT)&&log_[3].a[1]==(AT(A)&0xffff0000u));
    CHECK(log_[4].f==G(0x47ebb8)&&log_[4].r[2]==0x3c3c);
    CHECK(log_[5].f==G(0x47fa20)&&log_[5].a[0]==G(0x6c56a8));
    CHECK(log_[6].f==G(0x4801d4)&&log_[7].f==G(0x4089bc)&&log_[7].r[2]==0x1000*3);
}

int main(void){
    test_hit();test_break();
    printf("region_break: %u checks passed\n",checks);
    return 0;
}
