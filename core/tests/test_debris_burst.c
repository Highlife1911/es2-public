#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/debris_burst.h"
/* Synthetic guest: tables, pieces, shapes and objects at 90000000h, an
 * image base I with the globals the seven functions read, a frame scratch
 * at A0000000h. Every game call is logged with its arguments and entry
 * registers; the stubs return planned values and write planned outputs.
 * The expectations below are read from the reference listing
 * (hit_tests.md, section "debris burst"). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define I UINT32_C(0x10000000)
#define G(va) (I+(va)-0x400000u)
#define F(x) (FRAME+ES2_DEBRIS_FRAME-(x))
static uint8_t arena[0x1000],scratch[ES2_DEBRIS_SCRATCH],globals1[0x40],globals2[0x10];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"debris_burst line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4a9640)<sizeof(globals1))return globals1+(a-G(0x4a9640));
    if(a-G(0x6cbee0)<sizeof(globals2))return globals2+(a-G(0x6cbee0));
    fprintf(stderr,"debris_burst: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[5],r[3]; unsigned n; } logged;
static logged log_[64];static unsigned calls;
static uint32_t rng_values[8],alloc_result;static unsigned rng_index;
#define T (OBJ+0x100)
#define PIECES (OBJ+0x200)
#define SHAPES (OBJ+0x300)
#define POS (OBJ+0x340)
#define MAT (OBJ+0x360)
#define O (OBJ+0x400)
#define COUNTER (OBJ+0x800)
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls++;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(f==G(0x49681a)){CHECK(n==3);memset(host(0,a[0]),a[1],a[2]);r->eax=a[0];r->edx=0x1a1a0000u;r->ecx=0;}
    else if(f==G(0x49630c)){CHECK(n==0&&r->eax==G(0x497d70));w32(0,F(0x24),0xcafef00du);r->eax=0x1234u;r->ecx=0x4321u;}
    else if(f==G(0x492f24)){CHECK(n==2&&a[0]==G(0x4d2599));r->eax=rng_values[rng_index++&7];r->edx=r->eax;r->ecx=0x77770000u|(uint16_t)a[1];}
    else if(f==G(0x492ee0)){CHECK(n==1&&a[0]==G(0x4d2599));r->eax=0x9abcu;r->edx=0x5eu;r->ecx=0x5fu;}
    else if(f==G(0x471b30)){CHECK(n==1);r->eax=alloc_result;r->edx=0x71u;r->ecx=0x72u;}
    else if(f==G(0x402188)){CHECK(n==2);r->eax=0x21u;r->edx=0x22u;r->ecx=0x23u;}
    else if(f==G(0x47ebb8)){CHECK(n==2);memset(host(0,a[1]),0x11,20);r->eax=a[1];r->edx=0xe1u;r->ecx=0xe2u;}
    else if(f==G(0x47f058)){CHECK(n==3);memset(host(0,a[2]),0x22,20);r->eax=a[2];r->edx=0xf1u;r->ecx=0xf2u;}
    else if(f==G(0x47f9a0)){CHECK(n==2);w16(0,a[1],0x1001);w16(0,a[1]+2,0x2002);w16(0,a[1]+4,0x3003);r->eax=a[1];r->edx=0xabcd0000u;r->ecx=0x93u;}
    else if(f==G(0x4089e4)){CHECK(n==4);r->eax=0x41u;r->edx=0x42u;r->ecx=0x43u;}
    else if(f==G(0x4086bc)||f==G(0x408588)||f==G(0x408940)||f==G(0x408b74)||f==G(0x408bb4)){r->eax=0xb0u+n;r->edx=0xc0u+n;r->ecx=0xd0u;}
    else {fprintf(stderr,"debris_burst: unexpected call %08x\n",f);exit(1);}
}
static es2_weapon_graph graph(void){es2_weapon_graph g={I,FRAME,0,0,0,call,0,0,0};return g;}
static const uint32_t entry[3]={0x11110001u,0x22220002u,0x33330003u};
static void reset(void){
    memset(arena,0,sizeof(arena));memset(scratch,0x5a,sizeof(scratch));memset(globals1,0,sizeof(globals1));memset(globals2,0,sizeof(globals2));
    w32(0,G(0x6cbee4),COUNTER);w32(0,COUNTER,40);calls=0;rng_index=0;
}
static void table_checks(void){
    reset();
    /* Default list: count 4 > index 3. */
    w16(0,G(0x4a9654),4);w32(0,G(0x4a964c),OBJ+0x900);w16(0,T,3);
    CHECK(es2_debris_table_registers(&memory,I,T)==((uint64_t)4<<32|G(0x4a9650))&&r16(0,T)==3);
    /* Equal: the second list, index lowered. */
    w16(0,T,4);CHECK(es2_debris_table_registers(&memory,I,T)==((uint64_t)4<<32|(OBJ+0x900))&&r16(0,T)==0);
    /* Negative count: EDX sign-extended, index raised by the 16-bit SUB. */
    w16(0,G(0x4a9654),0xfffe);w16(0,T,0xffff);
    CHECK(es2_debris_table_registers(&memory,I,T)==((uint64_t)0xfffffffeu<<32|(OBJ+0x900))&&r16(0,T)==1);
    /* Negative index below a positive count. */
    w16(0,G(0x4a9654),2);w16(0,T,0x8000);CHECK((uint32_t)es2_debris_table_registers(&memory,I,T)==G(0x4a9650)&&r16(0,T)==0x8000);
}
static void burst_checks(void){
    es2_weapon_graph g=graph();reset();
    w32(0,POS,0x10);w32(0,POS+4,0x20);w32(0,POS+8,0x30);
    uint64_t v=es2_debris_burst_registers(&memory,&g,T,SHAPES,POS,0x99,entry);
    CHECK(calls==2&&log_[0].f==G(0x49681a)&&log_[0].a[0]==F(0x20)&&log_[0].a[1]==0&&log_[0].a[2]==0x20);
    CHECK(log_[0].r[0]==F(0x20)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x4086bc)&&log_[1].a[0]==T&&log_[1].a[1]==SHAPES&&log_[1].a[2]==F(0x20)&&log_[1].a[3]==0x99);
    CHECK(log_[1].r[0]==F(0x20)&&log_[1].r[1]==SHAPES&&log_[1].r[2]==0x99&&v==((uint64_t)0xc4<<32|0xb4));
    /* Identity rotation, mode 0, translation from the point. */
    CHECK(r16(0,F(0x20))==0x4000&&r16(0,F(0x1a))==0x4000&&r16(0,F(0x10))==0x4000&&r16(0,F(0x1e))==0&&scratch[ES2_DEBRIS_FRAME-0xe]==0);
    CHECK(r32(0,F(0xc))==0x10&&r32(0,F(8))==0x20&&r32(0,F(4))==0x30);
}
static void piece(unsigned i,uint16_t shape,uint16_t weight,uint16_t b,uint16_t w,uint16_t angle,uint16_t heading,uint16_t speed){
    uint32_t p=PIECES+14u*i;w16(0,p,shape);w16(0,p+2,weight);w16(0,p+4,b);w16(0,p+6,w);w16(0,p+8,angle);w16(0,p+10,heading);w16(0,p+12,speed);
}
static void pick_checks(void){
    es2_weapon_graph g=graph();reset();
    /* Count 2, bound 10: weights 3, 4, 5. Draw 7 skips two pieces (7-3 =
     * 4, not > 4), draw 3 stays on the first. */
    w16(0,T,2);w32(0,T+4,PIECES);w16(0,T+8,10);piece(0,0,3,0,0,0,0,0);piece(1,0,4,0,0,0,0,0);piece(2,0,5,0,0,0,0,0);
    rng_values[0]=7;rng_values[1]=3;
    uint64_t v=es2_debris_pick_registers(&memory,&g,T,SHAPES,MAT,0x55,entry);
    CHECK(calls==4&&log_[0].f==G(0x492f24)&&log_[0].a[1]==((entry[0]&0xffff0000u)|10)&&log_[0].r[0]==log_[0].a[1]);
    CHECK(log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x408588)&&log_[1].a[0]==SHAPES&&log_[1].a[1]==PIECES+14&&log_[1].a[2]==MAT&&log_[1].a[3]==0x55);
    CHECK(log_[1].r[0]==SHAPES&&log_[1].r[1]==MAT&&log_[1].r[2]==0x77770004u);
    CHECK(log_[2].a[1]==10u&&log_[2].r[0]==10u&&log_[2].r[1]==0xc4u&&log_[2].r[2]==0xd0u);
    CHECK(log_[3].a[1]==PIECES&&log_[3].r[2]==0x77770003u&&v==((uint64_t)0xc4<<32|0xb4));
    /* Negative count: no pick; EAX keeps the count word. */
    reset();w16(0,T,0xfffe);v=es2_debris_pick_registers(&memory,&g,T,SHAPES,MAT,0x55,entry);
    CHECK(calls==0&&v==((uint64_t)entry[1]<<32|(entry[0]&0xffff0000u)|0xfffe));
    /* Count 0: every piece in order, T+2 of them; EAX = flag, EDX = M,
     * ECX = shapes. */
    reset();w16(0,T,0);w16(0,T+2,3);w32(0,T+4,PIECES);v=es2_debris_pick_registers(&memory,&g,T,SHAPES,MAT,0x55,entry);
    CHECK(calls==3&&log_[0].a[1]==PIECES&&log_[1].a[1]==PIECES+14&&log_[2].a[1]==PIECES+28);
    CHECK(log_[2].r[0]==0x55&&log_[2].r[1]==MAT&&log_[2].r[2]==SHAPES&&v==((uint64_t)0xc4<<32|0xb4));
    reset();w16(0,T,0);w16(0,T+2,0x8000);v=es2_debris_pick_registers(&memory,&g,T,SHAPES,MAT,0x55,entry);
    CHECK(calls==0&&v==((uint64_t)entry[1]<<32|(entry[0]&0xffff0000u)));
}
static void piece_checks(void){
    es2_weapon_graph g=graph();reset();
    w32(0,G(0x4a9642),0x4a4a4a4au);w32(0,SHAPES+8,0x5a5a0000u);
    /* Allocation fails: EAX = 0, EDX = pool_alloc's. */
    alloc_result=0;piece(0,2,0,0x1ff,0x8001,5,6,7);
    uint64_t v=es2_debris_piece_registers(&memory,&g,SHAPES,PIECES,MAT,0,entry);
    CHECK(calls==2&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]&&log_[1].f==G(0x471b30)&&log_[1].a[0]==0x4a4a4a4au);
    CHECK(log_[1].r[1]==0x4a4a4a4au&&v==((uint64_t)0x71<<32)&&r32(0,F(4))==0);
    /* Both orientation words: angles, compose, matrix_angles, spin with
     * the heading f-28h + piece+0Ah - piece+8; the angles land in O+0Ch. */
    reset();w32(0,G(0x4a9642),0x4a4a4a4au);w32(0,SHAPES+8,0x5a5a0000u);alloc_result=O;piece(0,2,0,0x1ff,0x8001,0x100,0x30,0xfff0);
    v=es2_debris_piece_registers(&memory,&g,SHAPES,PIECES,MAT,0,entry);
    CHECK(calls==7&&log_[2].f==G(0x408940)&&log_[2].a[0]==O&&log_[2].a[1]==0x5a5a0000u&&log_[2].a[2]==MAT+0x14);
    CHECK(log_[2].a[3]==0x1ff&&log_[2].a[4]==0xffff8001u&&log_[2].r[0]==O&&log_[2].r[1]==SHAPES&&log_[2].r[2]==0x5a5a0000u);
    CHECK(r32(0,COUNTER)==36&&r16(0,F(0x14))==8&&r32(0,F(0x24))==0xcafef00du);
    CHECK(log_[3].f==G(0x47ebb8)&&log_[3].a[0]==F(0x34)&&log_[3].a[1]==F(0x48)&&log_[3].r[2]==O&&log_[3].r[1]==F(0x48));
    CHECK(r16(0,F(0x34))==0&&r16(0,F(0x32))==0&&r16(0,F(0x30))==0x100);
    CHECK(log_[4].f==G(0x47f058)&&log_[4].a[0]==F(0x48)&&log_[4].a[1]==MAT&&log_[4].a[2]==F(0x5c)&&log_[4].r[0]==MAT&&log_[4].r[2]==F(0x48));
    CHECK(log_[5].f==G(0x47f9a0)&&log_[5].a[0]==F(0x5c)&&log_[5].a[1]==F(0x2c)&&log_[5].r[0]==F(0x2c)&&log_[5].r[1]==F(0x5c)&&log_[5].r[2]==0xf2u);
    CHECK(log_[6].f==G(0x408b74)&&log_[6].a[0]==O&&log_[6].a[1]==0xfffffff0u&&log_[6].a[2]==(0xabcd0000u|(uint16_t)(0x3003+0x30-0x100)));
    CHECK(log_[6].r[0]==log_[6].a[2]&&log_[6].r[1]==log_[6].a[2]&&log_[6].r[2]==0xfffffff0u);
    CHECK(r32(0,O+0xc)==0x20021001u&&r16(0,O+0x10)==0x3003&&r16(0,O+0x32)==0&&v==((uint64_t)0x20023003u<<32|(O+0xc)));
    /* Angle only: random heading through debris_spin_any. */
    reset();w32(0,G(0x4a9642),1);alloc_result=O;piece(0,0,0,0,0,0x100,0xffff,0x22);w32(0,SHAPES,0x5a5a0000u);
    v=es2_debris_piece_registers(&memory,&g,SHAPES,PIECES,MAT,0,entry);
    CHECK(calls==7&&log_[6].f==G(0x408bb4)&&log_[6].a[0]==O&&log_[6].a[1]==0x22&&log_[6].r[0]==0x22&&log_[6].r[1]==0xabcd0000u&&log_[6].r[2]==0x93u);
    CHECK(r16(0,O+0x10)==0x3003&&(uint32_t)v==O+0xc);
    /* Heading only: angles (0, 0, -1) still computed; no O+0Ch write;
     * the spin's EDX:EAX are returned. */
    reset();w32(0,G(0x4a9642),1);alloc_result=O;piece(0,0,0,0,0,0xffff,0x40,0x22);
    v=es2_debris_piece_registers(&memory,&g,SHAPES,PIECES,MAT,0,entry);
    CHECK(calls==7&&r16(0,F(0x30))==0xffff&&log_[6].f==G(0x408b74)&&log_[6].a[2]==(0xabcd0000u|(uint16_t)(0x3003+0x40+1)));
    CHECK(r32(0,O+0xc)==0&&v==((uint64_t)0xc3<<32|0xb3));
    /* Neither: no angles; debris_spin_any with EDX = the counter pointer,
     * ECX = the object. */
    reset();w32(0,G(0x4a9642),1);alloc_result=O;piece(0,0,0,0,0,0xffff,0xffff,0x22);
    v=es2_debris_piece_registers(&memory,&g,SHAPES,PIECES,MAT,0,entry);
    CHECK(calls==4&&log_[3].f==G(0x408bb4)&&log_[3].r[0]==0x22&&log_[3].r[1]==COUNTER&&log_[3].r[2]==O&&v==((uint64_t)0xc2<<32|0xb2));
}
static void init_checks(void){
    es2_weapon_graph g=graph();reset();
    w32(0,POS,0x10);w32(0,POS+4,0x20);w32(0,POS+8,0x30);w32(0,G(0x4a964c),0x4c4c4c4cu);rng_values[0]=0xfffff000u;
    uint64_t v=es2_debris_init_registers(&memory,&g,O,0x5a5a0000u,POS,0x12345,0xabcd8001u,entry);
    CHECK(calls==2&&log_[0].f==G(0x402188)&&log_[0].a[0]==O&&log_[0].a[1]==0x5a5a0000u&&log_[0].r[0]==0x5a5a0000u&&log_[0].r[1]==entry[1]);
    CHECK(log_[1].a[1]==30000&&log_[1].r[0]==0x45&&log_[1].r[1]==0x8001u&&log_[1].r[2]==0x10);
    CHECK(r32(0,O)==G(0x497ed8)&&r16(0,O+4)==1&&r32(0,O+0x26)==0x10&&r32(0,O+0x2a)==0x20&&r32(0,O+0x2e)==0x30&&r32(0,O+0x50)==0);
    CHECK(arena[O-OBJ+0x41]==0x45&&r16(0,O+0x59)==0x8001&&r32(0,O+0x55)==0xfffff7d0u&&r32(0,O+0x5d)==0x4c4c4c4cu&&r32(0,COUNTER)==41);
    CHECK(v==((uint64_t)0x4c4c4c4cu<<32|O));
}
static void spin_checks(void){
    es2_weapon_graph g=graph();reset();
    w16(0,G(0x4a9664),3000);w16(0,G(0x4a9666),8000);rng_values[0]=0x00010123u;
    uint64_t v=es2_debris_spin_registers(&memory,&g,O,0x77,0xffff1234u,entry);
    CHECK(calls==2&&log_[0].a[1]==((entry[0]&0xffff0000u)|5000)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x4089e4)&&log_[1].a[0]==O&&log_[1].a[1]==0x77&&log_[1].a[2]==(0x00010000u|(uint16_t)(0x123+3000))&&log_[1].a[3]==0x00011234u);
    CHECK(log_[1].r[0]==0x77&&log_[1].r[1]==0x00011234u&&log_[1].r[2]==0x77771388u&&v==((uint64_t)0x42<<32|0x41));
    reset();v=es2_debris_spin_any_registers(&memory,&g,O,0x77,entry);
    CHECK(calls==2&&log_[0].f==G(0x492ee0)&&log_[0].r[0]==entry[0]&&log_[1].f==G(0x408b74)&&log_[1].a[2]==0x9abc);
    CHECK(log_[1].r[0]==0x77&&log_[1].r[1]==0x5e&&log_[1].r[2]==G(0x4d2599)&&v==((uint64_t)0xc3<<32|0xb3));
}
int main(void){
    table_checks();burst_checks();pick_checks();piece_checks();init_checks();spin_checks();
    printf("PASS: debris_burst %u checks\n",checks);return 0;
}
