#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/terrain_load.h"
/* Synthetic guest: an arena at 90000000h and a synthetic image base I with
 * the globals the six functions touch in a few small blocks. Game calls are
 * logged and answered by a stub (stream reads and _fscanf copy scripted
 * values, _itoa writes its digits, the stream constructor its vtable).
 * Expectations worked out by hand from the reference listing
 * (docs/reverse-engineering/terrain.md, section "the terrain zone
 * load"). */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define VA(x) (I+(uint32_t)(x)-0x400000u)
static uint8_t arena[0x3000];
static struct { uint32_t va; uint8_t bytes[0x400]; } blocks[]={
    {0x49aa00,{0}},{0x4aaa00,{0}},{0x4d1f00,{0}},{0x4a0900,{0}},{0x6cbe00,{0}},{0x4d2500,{0}},{0x6b5400,{0}}};
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"terrain_load line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    for(unsigned i=0;i<sizeof(blocks)/sizeof(blocks[0]);++i)
        if(a-VA(blocks[i].va)<sizeof(blocks[i].bytes))return blocks[i].bytes+(a-VA(blocks[i].va));
    fprintf(stderr,"terrain_load: unmapped %08x\n",a);exit(1);
}
static uint8_t r8(uint32_t a){return *(uint8_t *)host(0,a);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static const es2_guest_memory *m=&memory;
/* The stream S's vtable VT (slot +18h: READ), the bitmap's vtable BVT (slot
 * +18h: DTOR); the counter word COUNT behind the pointer [6CBEE4h]. */
#define VT (ARENA+0x2f00u)
#define BVT (ARENA+0x2f40u)
#define COUNT (ARENA+0x2f80u)
#define READ UINT32_C(0x77770018)
#define DTOR UINT32_C(0x88880018)
typedef struct call_record { uint32_t target,args[6],regs[3];unsigned n; } call_record;
static call_record log_[64];static unsigned calls;
static uint32_t source[32];static unsigned source_at;   /* dwords the reads and _fscanf deliver */
static uint32_t answer_eax[64];static int answered[64];
static uint32_t stream_status,reread_grid,reread_at;
static void stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;CHECK(calls<64);call_record *v=log_+calls;
    v->target=target;v->n=n;memcpy(v->args,a,4*n);v->regs[0]=r->eax;v->regs[1]=r->edx;v->regs[2]=r->ecx;
    if(target==READ&&a[1]==4)w32(0,a[2],source[source_at++]);
    if(target==VA(0x4735b8)){w32(0,a[2],source[source_at]);w32(0,a[2]+4,source[source_at+1]);source_at+=2;}
    if(target==VA(0x4967b4))for(unsigned k=2;k<n;++k)w32(0,a[k],source[source_at++]);
    if(target==VA(0x472f14)){w32(0,a[0],VT);w32(0,a[0]+4,stream_status);}
    if(target==READ&&reread_grid&&a[2]==reread_at)w32(0,VA(0x49aac8),reread_grid);
    if(target==VA(0x496820)){char text[16];snprintf(text,sizeof text,"%d",(int)a[0]);for(unsigned k=0;k<=strlen(text);++k)w8(a[1]+k,(uint8_t)text[k]);}
    r->eax=answered[calls]?answer_eax[calls]:0x11110001u+calls;r->edx=0x22220000u+calls;r->ecx=0x33330000u+calls;
    ++calls;
}
static uint32_t observed[64][2];static unsigned observations;
static void observe(void *context,uint32_t function,unsigned slot){(void)context;observed[observations][0]=function;observed[observations][1]=slot;++observations;}
static es2_weapon_graph graph={I,0,0,0,0,stub,NULL,observe,NULL};
static const uint32_t entry[3]={0xabcd1234u,0x5678ef01u,0x0badf00du};
#define EAX(p) ((uint32_t)(p))
#define EDX(p) ((uint32_t)((p)>>32))
static void answer(unsigned k,uint32_t eax){answer_eax[k]=eax;answered[k]=1;}
static void reset(void){
    memset(arena,0x5a,sizeof(arena));calls=0;observations=0;source_at=0;
    memset(answer_eax,0,sizeof(answer_eax));memset(answered,0,sizeof(answered));memset(source,0,sizeof(source));
    stream_status=0;reread_grid=0;reread_at=0;
    for(unsigned i=0;i<sizeof(blocks)/sizeof(blocks[0]);++i)memset(blocks[i].bytes,0,sizeof(blocks[i].bytes));
    w32(0,VT+0x18,READ);w32(0,BVT+0x18,DTOR);w32(0,VA(0x6cbee4),COUNT);w32(0,COUNT,100);
}
static int regs(unsigned k,uint32_t eax,uint32_t edx,uint32_t ecx){return log_[k].regs[0]==eax&&log_[k].regs[1]==edx&&log_[k].regs[2]==ecx;}
static int call(unsigned k,uint32_t target,unsigned n){return log_[k].target==target&&log_[k].n==n;}

static void mark(void){
    const uint32_t G=ARENA+0x100,CELLS=ARENA+0x400,MARKS=ARENA+0x800;uint32_t ecx=0;
    reset();w32(0,G+0xec,CELLS);w32(0,G+0xf0,MARKS);
    uint64_t p=es2_cell_mark_set_registers(m,G,CELLS+16*5+3,0x1ff,entry,&ecx);
    /* (cell - base) >> 4 = 5 (the unsigned shift drops the 3), the low byte */
    CHECK(r8(MARKS+5)==0xff&&r8(MARKS+4)==0x5a&&r8(MARKS+6)==0x5a&&EAX(p)==MARKS&&EDX(p)==5&&ecx==0x0badf0ffu);
    /* a cell below the base: the unsigned shift of the wrapped difference
     * FFFFFFF0h gives 0FFFFFFFh; marks chosen so that the byte lands at
     * MARKS */
    reset();w32(0,G+0xec,CELLS);w32(0,G+0xf0,MARKS-0x0fffffffu);
    p=es2_cell_mark_set_registers(m,G,CELLS-0x10,7,entry,NULL);
    CHECK(EDX(p)==0x0fffffffu&&EAX(p)==MARKS-0x0fffffffu&&r8(MARKS)==7);
    (void)p;
}
static void cache(void){
    const uint32_t C=ARENA+0x100;uint32_t ecx=0;
    reset();answer(0,ARENA+0x400);answer(1,ARENA+0x500);answer(2,ARENA+0x600);
    uint64_t p=es2_height_cache_ctor_registers(m,&graph,C,0x100,entry,&ecx);
    CHECK(r32(0,C)==VA(0x4a6a40)&&r32(0,C+4)==100&&r32(0,C+8)==0&&r32(0,C+0xc)==0x100);
    CHECK(r32(0,C+0x10)==ARENA+0x400&&r32(0,C+0x14)==ARENA+0x500&&r32(0,C+0x18)==ARENA+0x600&&r32(0,COUNT)==102);
    CHECK(calls==3&&call(0,VA(0x4774f0),1)&&log_[0].args[0]==0x100&&regs(0,0x100,0x100,0));
    CHECK(call(1,VA(0x49667c),5)&&log_[1].args[0]==0&&log_[1].args[1]==0xc&&log_[1].args[2]==0x100&&log_[1].args[3]==1&&
          log_[1].args[4]==VA(0x4953c4)&&regs(1,ARENA+0x400,0x100,0x100));
    CHECK(call(2,VA(0x49667c),5)&&log_[2].args[2]==8&&regs(2,ARENA+0x500,0x22220001u,0x33330001u));
    CHECK(EAX(p)==C&&EDX(p)==0x22220002u&&ecx==COUNT);
    CHECK(observations==3&&observed[0][0]==0x4774f0u&&observed[2][0]==0x49667cu);
}
static void grid(void){
    const uint32_t G=ARENA+0x100;uint32_t ecx=0;
    reset();
    uint64_t p=es2_grid_ctor_registers(m,&graph,G,entry,&ecx);
    CHECK(r32(0,G)==VA(0x4a0c74)&&r32(0,G+0xf4)==0&&r32(0,G+0xf8)==0&&r32(0,G+0xfc)==0&&r32(0,G+0x108)==10&&
          r32(0,G+0x10c)==8&&r32(0,COUNT)==102&&r32(0,G+0x100)==0x5a5a5a5au);
    CHECK(calls==2&&call(0,VA(0x49667c),5)&&log_[0].args[0]==G+0x28&&log_[0].args[1]==8&&log_[0].args[2]==0x14&&
          log_[0].args[3]==1&&log_[0].args[4]==VA(0x49545b)&&regs(0,COUNT,G+0x28,entry[2]));
    CHECK(call(1,VA(0x495268),2)&&log_[1].args[0]==G+0xd0&&log_[1].args[1]==0x100&&regs(1,0x11110001u,0x22220000u,G+0xd0));
    CHECK(EAX(p)==G&&EDX(p)==COUNT&&ecx==0x33330001u);
}
/* A 4 x 2 bitmap, b = 5, material level 6 and cell shift 14: block mask 1
 * (blocks of 2 x 2), anchors (0,0) and (2,0) of bitmap row 0 = grid row 1,
 * cells 4 and 6. Heights 5 15 25 35 (grid row 1), 45 55 65 0 (row 0). */
static void cells(void){
    const uint32_t G=ARENA+0x100,B=ARENA+0x300,PIX=ARENA+0x340,CELLS=ARENA+0x400,MARKS=ARENA+0x500,MAT=ARENA+0x600;
    uint32_t ecx=0;
    reset();
    w16(0,B+4,2);w16(0,B+6,4);w32(0,B+0xe,PIX);
    const uint8_t pixels[8]={10,20,30,40,50,60,70,5};memcpy(arena+(PIX-ARENA),pixels,8);
    w32(0,G+0x121,MAT);w32(0,MAT+4,6);w32(0,G+0x108,14);w32(0,G+0x118,3);
    answer(0,CELLS);answer(1,MARKS);
    /* calls: 0 49667c, 1 4774f0, then per cell 470d74 (+ the draw of an
     * anchor): 2 cell4, 3 draw, 4 cell5, 5 cell6, 6 draw, 7 cell7, 8..11
     * cells 0..3, 12 4952e8, 13 4952fc */
    answer(3,0x777704cdu);answer(6,0x000014ceu);
    uint64_t p=es2_terrain_cells_registers(m,&graph,G,B,0x12345605u,entry,&ecx);
    CHECK(r32(0,G+0x100)==2&&r32(0,G+0x104)==1&&r32(0,G+0xec)==CELLS&&r32(0,G+0xf0)==MARKS);
    CHECK(calls==14&&call(0,VA(0x49667c),5)&&log_[0].args[0]==0&&log_[0].args[1]==0x10&&log_[0].args[2]==8&&
          log_[0].args[3]==1&&log_[0].args[4]==VA(0x4715e1)&&regs(0,2,1,1));
    CHECK(call(1,VA(0x4774f0),1)&&log_[1].args[0]==8&&regs(1,CELLS,0x22220000u,0x33330000u));
    static const unsigned order[8]={4,5,6,7,0,1,2,3};static const uint8_t height[8]={45,55,65,0,5,15,25,35};
    for(unsigned c=0;c<8;++c)CHECK(r8(CELLS+16*c)==height[c]);
    unsigned k=2;
    for(unsigned c=0;c<8;++c){
        const uint32_t cell=CELLS+16*order[c];
        CHECK(call(k,VA(0x470d74),3)&&log_[k].args[0]==G&&log_[k].args[1]==cell&&log_[k].args[2]==0);
        CHECK(log_[k].regs[0]==height[order[c]]+0u&&log_[k].regs[1]==height[order[c]]+0u&&log_[k].regs[2]==2);
        ++k;
        if(order[c]==4||order[c]==6){
            CHECK(call(k,VA(0x492ee0),1)&&log_[k].args[0]==VA(0x4d2599)&&regs(k,1,1,0xfffffffeu));++k;
        }
    }
    /* flags: (0x5a & 2) | material 1 << 2 for cell 4; the others copy */
    static const uint8_t flags[8]={6,6,2,2,6,6,2,2};
    for(unsigned c=0;c<8;++c)CHECK(r8(CELLS+16*c+15)==flags[c]);
    CHECK(r32(0,G+0x110)==0&&r32(0,G+0x114)==65u*3u);
    CHECK(call(12,VA(0x4952e8),3)&&log_[12].args[0]==G+0xd0&&log_[12].args[1]==0&&log_[12].args[2]==3&&
          regs(12,0,195,3));
    CHECK(call(13,VA(0x4952fc),1)&&log_[13].args[0]==G+0xd0&&regs(13,0x1111000du,0x2222000cu,0x3333000cu));
    CHECK(EAX(p)==0x1111000eu&&EDX(p)==0x2222000du&&ecx==G+0xd0);
    /* the marks themselves are the stub's (470d74 is a game call) */
    CHECK(r8(MARKS)==0x5a);
    /* widths 1 and -3: exponents 0, one cell; the block mask 0 draws for it */
    reset();w16(0,B+4,0xfffd);w16(0,B+6,1);w32(0,B+0xe,PIX);w8(PIX,9);
    w32(0,G+0x121,MAT);w32(0,MAT+4,8);w32(0,G+0x108,13);w32(0,G+0x118,2);answer(0,CELLS);answer(1,MARKS);answer(3,0x0fff);
    p=es2_terrain_cells_registers(m,&graph,G,B,9,entry,&ecx);
    CHECK(r32(0,G+0x100)==0&&r32(0,G+0x104)==0&&calls==6&&call(3,VA(0x492ee0),1)&&regs(3,0,0,0xffffffffu));
    CHECK(r8(CELLS)==0&&r8(CELLS+15)==2&&r32(0,G+0x114)==0);
    (void)p;
}
static void materials_dba(void){
    const uint32_t G=ARENA+0x100,NAME=ARENA+0x300,MPATH=ARENA+0x340,MATS=ARENA+0x380,OBJ=ARENA+0x3c0,ARR=ARENA+0x3e0,
        BM=ARENA+0x400,S0=ARENA+0x1000,F=S0+ES2_TERRAIN_MATERIALS_FRAME,S=F-0x30,T=F-0x60,COPY=F-0xc8;
    uint32_t ecx=0;
    reset();memset(arena+(S0-ARENA),0,ES2_TERRAIN_MATERIALS_SCRATCH);
    const char name[]="dba\\zone123.dba";memcpy(arena+(NAME-ARENA),name,sizeof name);
    w32(0,VA(0x4a09f0),VA(0x4a0a36));w32(0,F-0x84,0xfeedbeefu);
    w32(0,OBJ+8,ARR);w32(0,ARR,BM);w32(0,BM,BVT);
    source[0]=2;source[1]=0x10;source[2]=6;source[3]=0x11;source[4]=6;
    answer(1,MPATH);answer(4,MATS);answer(7,COPY+11);answer(8,0);answer(10,OBJ);answer(12,5);
    graph.scratch=S0;
    uint64_t p=es2_terrain_materials_registers(m,&graph,G,NAME,0x00000003u,entry,&ecx);
    graph.scratch=0;
    CHECK(!memcmp(arena+(COPY-ARENA),name,sizeof name)&&r16(0,F-0x74)==0x20);
    CHECK(calls==16&&call(0,VA(0x49630c),0)&&regs(0,VA(0x4a0c14),entry[1],entry[2]));
    CHECK(call(1,VA(0x492bec),2)&&log_[1].args[0]==VA(0x4a0c4d)&&log_[1].args[1]==VA(0x4a0a36)&&regs(1,VA(0x4a0a36),16,0));
    CHECK(call(2,VA(0x472f14),2)&&log_[2].args[0]==S&&log_[2].args[1]==MPATH&&regs(2,MPATH,S,0x33330001u));
    CHECK(call(3,READ,3)&&log_[3].args[0]==S&&log_[3].args[1]==4&&log_[3].args[2]==G+0x11d&&regs(3,G+0x11d,S,VT));
    CHECK(call(4,VA(0x4774f0),1)&&log_[4].args[0]==16&&regs(4,16,0x22220003u,0x33330003u));
    CHECK(r32(0,G+0x11d)==2&&r32(0,G+0x121)==MATS);
    CHECK(call(5,VA(0x4735b8),3)&&log_[5].args[0]==S&&log_[5].args[1]==8&&log_[5].args[2]==MATS&&regs(5,MATS,S,16));
    CHECK(call(6,VA(0x4735b8),3)&&log_[6].args[2]==MATS+8&&regs(6,MATS+8,S,0x33330005u));
    CHECK(r32(0,MATS+4)==6&&r32(0,MATS+8)==0x11);
    CHECK(call(7,VA(0x4967a2),2)&&log_[7].args[0]==COPY&&log_[7].args[1]==0x2e&&regs(7,0x11110007u,0x22220006u,COPY));
    CHECK(call(8,VA(0x496868),2)&&log_[8].args[0]==COPY+11&&log_[8].args[1]==VA(0x4a0c52)&&regs(8,COPY+11,0x22220007u,0x33330007u));
    CHECK(call(9,VA(0x472f14),2)&&log_[9].args[0]==T&&log_[9].args[1]==COPY&&regs(9,COPY,T,0x33330008u));
    CHECK(call(10,VA(0x47a144),2)&&log_[10].args[0]==T&&log_[10].args[1]==0&&regs(10,0x1111000au,0x22220009u,T));
    CHECK(call(11,VA(0x46c4c4),3)&&log_[11].args[0]==G&&log_[11].args[1]==BM&&log_[11].args[2]==(ARR&0xffffff00u|3u)&&
          regs(11,ARR&0xffffff00u|3u,0x2222000au,0x3333000au));
    CHECK(r32(0,F-0x64)==BM&&r32(0,COUNT)==105);
    CHECK(call(12,VA(0x496406),2)&&log_[12].args[0]==BM&&log_[12].args[1]==0&&regs(12,0x1111000cu,BM,0x3333000bu));
    CHECK(call(13,DTOR,2)&&log_[13].args[0]==BM&&log_[13].args[1]==3&&regs(13,BM,BVT,COUNT));
    CHECK(call(14,VA(0x472f64),2)&&log_[14].args[0]==T&&log_[14].args[1]==2&&regs(14,0x1111000eu,0x2222000du,T));
    CHECK(call(15,VA(0x472f64),2)&&log_[15].args[0]==S&&regs(15,S,0x2222000eu,0x3333000eu));
    CHECK(EAX(p)==0x11110010u&&EDX(p)==0xfeedbeefu&&ecx==0x3333000fu);
    CHECK(observations==16&&observed[3][0]==READ&&observed[3][1]==0x18&&observed[13][0]==DTOR&&observed[13][1]==0x18);
}
/* The text grid: no '.' in the name. Shift 14: s = 1. "%d %d %d %d" = 1, 1,
 * 2560, 512: four cells, top 1280 << 3, bottom 256 << 3 = 2048, scale
 * (10240 - 2048) >> 8 = 32. Heights 20, 40, 60, 80 >> 1. Three materials:
 * draws k = 1, 2 until one is below 4CEh. */
static void materials_text(void){
    const uint32_t G=ARENA+0x100,NAME=ARENA+0x300,MPATH=ARENA+0x340,MATS=ARENA+0x380,FILEP=ARENA+0x3c0,CELLS=ARENA+0x400,
        MARKS=ARENA+0x500,S0=ARENA+0x1000,F=S0+ES2_TERRAIN_MATERIALS_FRAME,S=F-0x30,COPY=F-0xc8;
    uint32_t ecx=0;
    reset();memset(arena+(S0-ARENA),0,ES2_TERRAIN_MATERIALS_SCRATCH);
    memcpy(arena+(NAME-ARENA),"grid",5);
    w32(0,VA(0x4a09f0),VA(0x4a0a36));w32(0,F-0x84,0x1234u);w32(0,G+0x108,14);w32(0,G+0xf0,MARKS);
    /* reads: the count 3, three records (two dwords each); _fscanf: the four
     * fields, then one height per cell */
    static const uint32_t script[15]={3,0,1,2,3,4,5,1,1,2560,512,20,40,60,80};
    memcpy(source,script,sizeof script);
    /* calls: 0 49630c, 1 492bec, 2 472f14, 3 read, 4 new[], 5..7 records,
     * 8 strchr (0), 9 fopen, 10 fscanf, 11 49667c, then per cell fscanf and
     * the draws: 12 h, 13 draw ok; 14 h, 15 draw fail, 16 draw ok; 17 h,
     * 18, 19 fail; 20 h, 21 ok; 22 fclose, 23 4952e8, 24 4952fc, 25 472f64 */
    answer(1,MPATH);answer(4,MATS);answer(8,0);answer(9,FILEP);answer(11,CELLS);
    answer(13,0x0001);answer(15,0x04ce);answer(16,0xf4cd);answer(18,0x7fff);answer(19,0x1fff);answer(21,0x8000);
    graph.scratch=S0;
    uint64_t p=es2_terrain_materials_registers(m,&graph,G,NAME,0,entry,&ecx);
    graph.scratch=0;
    CHECK(calls==26&&call(8,VA(0x4967a2),2)&&call(9,VA(0x496802),2)&&log_[9].args[0]==COPY&&log_[9].args[1]==VA(0x4a0c57)&&
          regs(9,1,COPY,0x33330008u));
    CHECK(call(10,VA(0x4967b4),6)&&log_[10].args[0]==FILEP&&log_[10].args[1]==VA(0x4a0c59)&&log_[10].args[2]==F-0x9c&&
          log_[10].args[3]==F-0x98&&log_[10].args[4]==F-0x8c&&log_[10].args[5]==F-0x88&&regs(10,FILEP,F-0x98,F-0x9c));
    CHECK(r32(0,G+0x100)==1&&r32(0,G+0x104)==1&&r32(0,G+0xec)==CELLS&&r32(0,G+0x110)==2048&&r32(0,G+0x118)==32);
    CHECK(call(11,VA(0x49667c),5)&&log_[11].args[2]==4&&log_[11].args[4]==VA(0x4715e1)&&regs(11,2,1,1));
    CHECK(call(12,VA(0x4967b4),3)&&log_[12].args[0]==FILEP&&log_[12].args[1]==VA(0x4a0c65)&&log_[12].args[2]==F-0xa0&&
          regs(12,F-0xa0,FILEP,1));
    /* the first draw: EAX the cell, EDX the limit 4 with the height byte, ECX
     * columns + rows */
    CHECK(call(13,VA(0x492ee0),1)&&log_[13].args[0]==VA(0x4d2599)&&regs(13,CELLS,10,2));
    CHECK(call(15,VA(0x492ee0),1)&&regs(15,CELLS+16,20,2)&&call(16,VA(0x492ee0),1)&&regs(16,0x4ce,0,VA(0x4d2599)));
    CHECK(call(18,VA(0x492ee0),1)&&call(19,VA(0x492ee0),1)&&call(21,VA(0x492ee0),1));
    static const uint8_t height[4]={10,20,30,40},flags[4]={6,10,2,6};
    for(unsigned c=0;c<4;++c)CHECK(r8(CELLS+16*c)==height[c]&&r8(CELLS+16*c+15)==flags[c]&&r8(MARKS+c)==0);
    CHECK(call(22,VA(0x496838),1)&&log_[22].args[0]==FILEP&&regs(22,FILEP,0,1));
    CHECK(call(23,VA(0x4952e8),3)&&log_[23].args[0]==G+0xd0&&log_[23].args[1]==2048&&log_[23].args[2]==32&&regs(23,32,2048,FILEP));
    CHECK(call(24,VA(0x4952fc),1)&&call(25,VA(0x472f64),2)&&log_[25].args[0]==S&&regs(25,S,0x22220018u,G+0xd0));
    CHECK(EAX(p)==0x1111001au&&EDX(p)==0x1234u&&ecx==0x33330019u);
    (void)MPATH;(void)MATS;
}
static void zone(void){
    const uint32_t S0=ARENA+0x1000,F=S0+ES2_ZONE_LOAD_FRAME,S=F-0x30,G=ARENA+0x100,PATH1=ARENA+0x300,PATH2=ARENA+0x340;
    uint32_t ecx=0;
    /* negative zone word: only the object table and the three globals */
    reset();memset(arena+(S0-ARENA),0,ES2_ZONE_LOAD_SCRATCH);w8(VA(0x4d1f40),7);w32(0,F-0x58,0xc0ffee00u);
    w32(0,VA(0x4aaa74),1);w32(0,VA(0x4aaa78),2);w16(0,VA(0x4aaa7c),3);
    graph.scratch=S0;
    uint64_t p=es2_zone_load_registers(m,&graph,0x1238000u,entry,&ecx);
    CHECK(calls==1&&call(0,VA(0x49630c),0)&&regs(0,VA(0x49ab2a),entry[1],entry[2]));
    CHECK(r8(VA(0x49aaf0))==1&&r32(0,VA(0x4aaaa8))==7&&r32(0,VA(0x4aaaac))==1&&r32(0,VA(0x49aad0))==20000);
    CHECK(r32(0,VA(0x4aaa74))==0&&r32(0,VA(0x4aaa78))==0&&r16(0,VA(0x4aaa7c))==0&&r32(0,VA(0x4a0be0))==0);
    CHECK(EAX(p)==0&&EDX(p)==7&&ecx==0xc0ffee00u);
    /* zone 123: "zone123" through _itoa, four reads (8, 8, cell shift 16,
     * scale 256), the grid, "dba", the materials, the transform */
    reset();memset(arena+(S0-ARENA),0,ES2_ZONE_LOAD_SCRATCH);w32(0,F-0x58,0xc0ffee00u);
    w8(VA(0x49aaf0),2);w8(VA(0x4d1f40),1);w32(0,VA(0x4a09f0),VA(0x4a0a36));
    memcpy(blocks[0].bytes+0xf1,"zoneXXXX",9);
    source[0]=8;source[1]=8;source[2]=16;source[3]=256;
    answer(2,PATH1);answer(6,G);answer(10,PATH2);
    p=es2_zone_load_registers(m,&graph,0xffff007bu,entry,&ecx);
    graph.scratch=0;
    CHECK(calls==14&&r8(VA(0x49aaf0))==2&&!memcmp(arena+(F-0x64-ARENA),"zone123",8));
    CHECK(call(1,VA(0x496820),3)&&log_[1].args[0]==123&&log_[1].args[1]==F-0x60&&log_[1].args[2]==10&&
          regs(1,123,0x58585800u,F-0x60));
    CHECK(call(2,VA(0x492bec),2)&&log_[2].args[0]==F-0x64&&log_[2].args[1]==VA(0x4a0a36)&&regs(2,0x11110002u,VA(0x4a0a36),F-0x64));
    CHECK(call(3,VA(0x472f14),2)&&log_[3].args[0]==S&&log_[3].args[1]==PATH1&&regs(3,S,0x22220002u,0x33330002u));
    CHECK(call(4,READ,3)&&log_[4].args[2]==F-0x70&&regs(4,S,VT,F-0x70)&&call(5,READ,3)&&log_[5].args[2]==F-0x6c&&regs(5,S,VT,F-0x6c));
    CHECK(call(6,VA(0x47749c),1)&&log_[6].args[0]==0x129&&regs(6,0x11110006u,0x22220005u,0x33330005u));
    CHECK(call(7,VA(0x46befc),2)&&log_[7].args[0]==G&&log_[7].args[1]==F-0x70&&regs(7,G,F-0x70,G));
    CHECK(r32(0,COUNT)==96&&r16(0,F-0x48)==0x14&&r32(0,VA(0x49aac8))==G);
    CHECK(call(8,READ,3)&&log_[8].args[2]==F-0x74&&regs(8,VT,G,S)&&call(9,READ,3)&&log_[9].args[2]==F-0x68&&regs(9,S,VT,F-0x68));
    CHECK(r32(0,G+0x108)==16&&r32(0,G+0x118)==256&&r32(0,G+0x10c)==2&&r32(0,G+0xf4)==0&&r32(0,G+0xf8)==0&&r32(0,G+0xfc)==0);
    CHECK(call(10,VA(0x492bec),2)&&log_[10].args[0]==F-0x64&&log_[10].args[1]==VA(0x49aba0)&&regs(10,F-0x64,0,0));
    CHECK(call(11,VA(0x46c754),3)&&log_[11].args[0]==G&&log_[11].args[1]==PATH2&&log_[11].args[2]==0&&
          regs(11,G,0x2222000au,0x3333000au));
    CHECK(call(12,VA(0x47ebb8),2)&&log_[12].args[0]==F-0x7c&&log_[12].args[1]==VA(0x4aaa84)&&regs(12,0,F-0x7c,0));
    CHECK(r16(0,F-0x7c)==0&&r16(0,F-0x7a)==0&&r16(0,F-0x78)==0&&r32(0,VA(0x4aaa98))==0&&r32(0,VA(0x4a0be0))==G);
    CHECK(call(13,VA(0x472f64),2)&&log_[13].args[0]==S&&log_[13].args[1]==2&&regs(13,G,S,0x3333000cu));
    CHECK(EAX(p)==0x1111000eu&&EDX(p)==0x2222000du&&ecx==0xc0ffee00u);
    /* a nonzero stream status reports through 479c78; a null grid: zone_load
     * stores 0 to [49AAC8h] and rereads it after the next read, whose stub
     * stores another grid G2 there (the original would store at 108h) */
    const uint32_t G2=ARENA+0x200;
    reset();memset(arena+(S0-ARENA),0,ES2_ZONE_LOAD_SCRATCH);w32(0,F-0x58,0x77u);graph.scratch=S0;
    memcpy(blocks[0].bytes+0xf1,"zoneXXXX",9);
    source[0]=1;source[1]=2;source[2]=14;source[3]=9;stream_status=9;reread_grid=G2;reread_at=F-0x74;
    answer(2,PATH1);answer(7,0);answer(10,PATH2);
    p=es2_zone_load_registers(m,&graph,5,entry,&ecx);
    graph.scratch=0;
    CHECK(calls==14&&!memcmp(arena+(F-0x64-ARENA),"zone5",6));
    CHECK(call(4,VA(0x479c78),6)&&log_[4].args[0]==VA(0x6c56c8)&&log_[4].args[1]==5&&log_[4].args[2]==0&&
          log_[4].args[3]==VA(0x49ab94)&&log_[4].args[4]==0x49&&log_[4].args[5]==PATH1&&regs(4,9,0,0x33330003u));
    CHECK(call(7,VA(0x47749c),1)&&call(8,READ,3)&&regs(8,VT,0,S)&&r16(0,F-0x48)==8&&r32(0,COUNT)==100);
    CHECK(r32(0,VA(0x49aac8))==G2&&r32(0,G2+0x108)==14&&r32(0,G2+0x118)==9&&r32(0,G2+0x10c)==10);
    CHECK(call(11,VA(0x46c754),3)&&log_[11].args[0]==G2&&r32(0,VA(0x4a0be0))==G2&&ecx==0x77u);
    (void)p;
}
/* ---- (terrain.md, section "base flattening and the
 * base-mark raster"): a 4 x 4 grid (n = m = 2) at FG with the cells at FC
 * (height byte +0, flags byte +0Fh) and the marks at FM. The stub plays the
 * real callees: 470e00 and 470d74 through the core's own functions, 470ddc
 * as the byte store, 470fe8/471128 as the core's floods (base_flatten's
 * end-to-end case) unless `flat_floods` is 0; 46c2e0 answers fixed
 * registers. Calls are logged in their order. */
#define FG (ARENA+0x100u)
#define FC (ARENA+0x400u)
#define FM (ARENA+0x800u)
static call_record flat_log[256];static unsigned flat_calls,flat_observed;static int flat_floods;
static uint8_t flat_path[2][64];
static void flat_stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r);
static void flat_observe(void *context,uint32_t function,unsigned slot){(void)context;(void)function;(void)slot;++flat_observed;}
static es2_weapon_graph flat_graph={I,0,0,0,0,flat_stub,NULL,flat_observe,NULL};
static void flat_stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;CHECK(flat_calls<256);call_record *v=flat_log+flat_calls++;
    v->target=target;v->n=n;memcpy(v->args,a,4*n);v->regs[0]=r->eax;v->regs[1]=r->edx;v->regs[2]=r->ecx;
    const uint32_t entry_regs[3]={r->eax,r->edx,r->ecx};uint32_t ecx=0;uint64_t p=0;
    if(target==VA(0x470e00))p=es2_cell_flag_get_registers(m,a[0],a[1],a[2],&ecx);
    else if(target==VA(0x470d74))p=es2_cell_mark_set_registers(m,a[0],a[1],a[2],entry_regs,&ecx);
    else if(target==VA(0x470ddc)){
        const uint32_t c=r32(0,a[0]+0x100),cells=r32(0,a[0]+0xf0),index=(a[2]<<(c&31))+a[1];
        w8(cells+index,(uint8_t)a[3]);p=(uint64_t)index<<32|cells;ecx=(c&0xffffff00u)|(a[3]&0xffu);
    } else if(target==VA(0x470fe8)&&flat_floods){
        es2_flood_stack s={flat_path[0],64,0,0};p=es2_flood_sum_registers(m,&flat_graph,a[0],a[1],a[2],entry_regs,&s,&ecx);
    } else if(target==VA(0x471128)&&flat_floods){
        es2_flood_stack s={flat_path[1],64,0,0};p=es2_flood_fill_registers(m,&flat_graph,a[0],a[1],a[2],a[3],a[4],entry_regs,&s,&ecx);
    } else {p=(uint64_t)0x44440000u<<32|0x55550000u;ecx=0x66660000u;}
    r->eax=(uint32_t)p;r->edx=(uint32_t)(p>>32);r->ecx=ecx;
}
static int regs_of(unsigned k,uint32_t eax,uint32_t edx,uint32_t ecx){
    return flat_log[k].regs[0]==eax&&flat_log[k].regs[1]==edx&&flat_log[k].regs[2]==ecx;
}
static int flat_call(unsigned k,uint32_t target,uint32_t x,uint32_t y){
    return flat_log[k].target==VA(target)&&flat_log[k].args[0]==FG&&flat_log[k].args[1]==x&&flat_log[k].args[2]==y;
}
static uint32_t cell(uint32_t x,uint32_t y){return FC+16u*((y<<2)+x);}
static uint8_t height(uint32_t x,uint32_t y){return r8(cell(x,y));}
static uint8_t marks(uint32_t x,uint32_t y){return r8(FM+(y<<2)+x);}
static void flat_reset(void){
    reset();flat_calls=0;flat_observed=0;flat_floods=1;
    w32(0,FG+0xec,FC);w32(0,FG+0xf0,FM);w32(0,FG+0x100,2);w32(0,FG+0x104,2);
    for(uint32_t i=0;i<16;++i){w8(FC+16*i,(uint8_t)(10+i));w8(FM+i,0);}
}
static void flag_get(void){
    uint32_t ecx=0;
    flat_reset();w8(FM+7,0x93);
    uint64_t p=es2_cell_flag_get_registers(m,FG,3,1,&ecx);
    CHECK(EAX(p)==0x93&&EDX(p)==7&&ecx==2);
    /* only CL's low five bits shift: n = 22h acts as 2 */
    w32(0,FG+0x100,0x22);p=es2_cell_flag_get_registers(m,FG,1,1,&ecx);
    CHECK(EAX(p)==0&&EDX(p)==5&&ecx==0x22);
}
static void flood_sum(void){
    /* region {(1,1), (2,1), (1,2)}; heights 15, 16, 19 */
    flat_reset();w8(FM+5,1);w8(FM+6,1);w8(FM+9,1);w8(FM+15,0xf8);   /* (3,3): bits 3..7 only, unmarked */
    w32(0,VA(0x6b5470),100);w32(0,VA(0x6b5474),1);
    uint8_t path[8];es2_flood_stack s={path,8,0,0};uint32_t ecx=0;
    uint64_t p=es2_flood_sum_registers(m,&flat_graph,FG,1,1,entry,&s,&ecx);
    /* 25 flag reads (the start, 8 per visited cell) and 3 flag writes */
    CHECK(flat_calls==28&&!s.overflow&&s.depth_max==2&&flat_observed==28);
    /* the start: EAX = the index 5, EDX = its cell, ECX = n + m */
    CHECK(flat_call(0,0x470e00,1,1)&&regs_of(0,5,cell(1,1),4));
    /* the visit: mark | 2 with 470e00's EDX (the index) and ECX (n) */
    CHECK(flat_call(1,0x470ddc,1,1)&&flat_log[1].args[3]==3&&regs_of(1,3,5,2));
    /* neighbour (0,2), then (1,2) visited, whose eight neighbours come
     * before (2,2) of the start */
    CHECK(flat_call(2,0x470e00,0,2)&&flat_call(3,0x470e00,1,2)&&flat_call(4,0x470ddc,1,2));
    CHECK(flat_call(5,0x470e00,0,3)&&flat_call(11,0x470e00,1,1)&&flat_call(12,0x470e00,2,1)&&flat_call(13,0x470ddc,2,1));
    CHECK(flat_call(14,0x470e00,1,2)&&flat_call(21,0x470e00,3,0)&&flat_call(22,0x470e00,2,2)&&flat_call(27,0x470e00,2,0));
    CHECK(r32(0,VA(0x6b5470))==100+15+19+16&&r32(0,VA(0x6b5474))==4);
    CHECK(marks(1,1)==3&&marks(1,2)==3&&marks(2,1)==3&&marks(3,3)==0xf8&&marks(0,0)==0);
    /* the result: the last neighbour (2,0)'s flag read: 0, index 2 */
    CHECK(EAX(p)==0&&EDX(p)==2&&ecx==2);
    /* a 1 x 1 grid: every neighbour is outside; EAX = 1 << 0, EDX from
     * the last neighbour's entry: y - 1 set for (x-1, y-1), kept by the
     * two after it */
    flat_reset();w32(0,FG+0x100,0);w32(0,FG+0x104,0);w8(FM,1);w32(0,VA(0x6b5470),0);w32(0,VA(0x6b5474),0);
    s.depth_max=0;p=es2_flood_sum_registers(m,&flat_graph,FG,0,0,entry,&s,&ecx);
    CHECK(flat_calls==2&&EAX(p)==1&&EDX(p)==0xffffffffu&&ecx==0&&r32(0,VA(0x6b5470))==10&&r32(0,VA(0x6b5474))==1&&s.depth_max==0);
    /* outside the grid: EAX = 1 << n, EDX the entry's, no call */
    flat_reset();p=es2_flood_sum_registers(m,&flat_graph,FG,4,0,entry,&s,&ecx);
    CHECK(flat_calls==0&&EAX(p)==4&&EDX(p)==entry[1]&&ecx==2);
    flat_reset();p=es2_flood_sum_registers(m,&flat_graph,FG,0,0xffffffffu,entry,&s,&ecx);
    CHECK(flat_calls==0&&EAX(p)==4&&EDX(p)==entry[1]);
    /* a visited start: 470e00's registers */
    flat_reset();w8(FM+5,3);p=es2_flood_sum_registers(m,&flat_graph,FG,1,1,entry,&s,&ecx);
    CHECK(flat_calls==1&&EAX(p)==3&&EDX(p)==5&&ecx==2);
    /* the path stack: a line of three cells needs two entries */
    flat_reset();w8(FM+0,1);w8(FM+1,1);w8(FM+2,1);
    es2_flood_stack tiny={path,1,0,0};p=es2_flood_sum_registers(m,&flat_graph,FG,0,0,entry,&tiny,&ecx);
    CHECK(tiny.overflow==1&&tiny.depth_max==1&&marks(2,0)==3);
    (void)p;
}
static void flood_fill(void){
    /* the region of flood_sum after the sum (marks 3), fill h = 56h from
     * (1,1) with w = 0: the region and the spill cells (written by the
     * neighbours with w = 1 before their mark test) */
    flat_reset();w8(FM+5,3);w8(FM+6,3);w8(FM+9,3);
    uint8_t path[8];es2_flood_stack s={path,8,0,0};uint32_t ecx=0;
    uint64_t p=es2_flood_fill_registers(m,&flat_graph,FG,1,1,0x12345656u,0,entry,&s,&ecx);
    CHECK(flat_calls==28&&!s.overflow&&s.depth_max==2);
    CHECK(flat_call(1,0x470ddc,1,1)&&flat_log[1].args[3]==7&&flat_call(4,0x470ddc,1,2)&&flat_call(13,0x470ddc,2,1));
    CHECK(marks(1,1)==7&&marks(1,2)==7&&marks(2,1)==7);
    static const uint8_t want[16]={10,11,12,13, 14,0x56,0x56,0x56, 18,0x56,0x56,0x56, 22,0x56,0x56,25};
    for(uint32_t i=0;i<16;++i)CHECK(height(i&3,i>>2)==want[i]);
    CHECK(EAX(p)==0&&EDX(p)==2);
    /* w = 1 on an unmarked start: the height is written, nothing else;
     * the registers are 470e00's with EDX = the cell */
    flat_reset();p=es2_flood_fill_registers(m,&flat_graph,FG,3,3,0x77,1,entry,&s,&ecx);
    CHECK(flat_calls==1&&height(3,3)==0x77&&EAX(p)==0&&EDX(p)==cell(3,3)&&ecx==0x77);
    /* a 1 x 1 grid: EDX = y - 1 from the neighbour (x, y-1) */
    flat_reset();w32(0,FG+0x100,0);w32(0,FG+0x104,0);w8(FM,1);
    p=es2_flood_fill_registers(m,&flat_graph,FG,0,0,0x99,0,entry,&s,&ecx);
    CHECK(flat_calls==2&&EAX(p)==1&&EDX(p)==0xffffffffu&&height(0,0)==0x99&&marks(0,0)==5);
}
static void flatten(void){
    /* regions {(0,0), (1,0)} (heights 10, 11 -> 10) and {(3,3)} (25) */
    flat_reset();w8(FM+0,1);w8(FM+1,1);w8(FM+15,1);
    w8(cell(1,0),13);                                  /* 10 + 13 = 23, / 2 = 11 */
    uint32_t divide[2]={0,0},ecx=0;int fault=0;
    uint64_t p=es2_base_flatten_registers(m,&flat_graph,FG,entry,divide,&fault,&ecx);
    CHECK(!fault&&ecx==4&&EAX(p)==0x55550000u&&EDX(p)==0x44440000u);
    CHECK(height(0,0)==11&&height(1,0)==11&&height(0,1)==11&&height(1,1)==11&&height(2,1)==11&&height(2,0)==11);
    CHECK(height(3,3)==25&&height(3,0)==13&&height(2,2)==20);
    CHECK(marks(0,0)==7&&marks(1,0)==7&&marks(3,3)==7&&marks(2,2)==0);
    CHECK(r32(0,VA(0x6b5470))==25&&r32(0,VA(0x6b5474))==1);
    /* the scan's first calls: (0,0) with the row's registers (EAX = 0,
     * EDX = the row's cell, ECX = n + m), then the sum with EAX = 0 */
    CHECK(flat_call(0,0x470e00,0,0)&&regs_of(0,0,FC,4)&&flat_call(1,0x470fe8,0,0)&&flat_log[1].regs[0]==0);
    unsigned last=flat_calls-1,fills=0,sums=0,reads=0;
    for(unsigned k=0;k<flat_calls;++k){fills+=flat_log[k].target==VA(0x471128);sums+=flat_log[k].target==VA(0x470fe8);}
    for(unsigned k=0;k<flat_calls;++k)reads+=flat_log[k].target==VA(0x470e00)&&k<last&&flat_log[k].args[0]==FG;
    CHECK(fills==2&&sums==2&&flat_log[last].target==VA(0x46c2e0)&&flat_log[last].n==1&&flat_log[last].args[0]==FG);
    /* the fill of the first region: (0,0), q = 11, w = 0; EAX = q, EDX =
     * the remainder 1 */
    for(unsigned k=0;k<flat_calls;++k)if(flat_log[k].target==VA(0x471128)){
        CHECK(flat_log[k].args[3]==11&&flat_log[k].args[4]==0&&flat_log[k].regs[0]==11&&flat_log[k].regs[1]==1);break;}
    /* a zero count faults: no fill, divide = {sum, 0} */
    flat_reset();flat_floods=0;w8(FM+6,1);w32(0,VA(0x6b5470),5);
    p=es2_base_flatten_registers(m,&flat_graph,FG,entry,divide,&fault,&ecx);
    CHECK(fault==1&&divide[0]==0&&divide[1]==0&&flat_calls==8&&flat_call(7,0x470fe8,2,1));
    /* n = 31: S = 80000000h, no row; only the shading pass */
    flat_reset();w32(0,FG+0x100,31);fault=0;
    p=es2_base_flatten_registers(m,&flat_graph,FG,entry,divide,&fault,&ecx);
    CHECK(!fault&&flat_calls==1&&flat_log[0].target==VA(0x46c2e0)&&regs_of(0,1u<<31,entry[1],31)&&ecx==0x80000000u);
    (void)reads;(void)p;
}
static void raster(void){
    /* 8 x 8 grid (n = m = 3), cell shift 14 (s = 1); material record 2 at
     * T + 16 with level 6: x0 SAR 15 = 3 -> bx = 3 << 1 = 6; y0 SAR 15 = 2
     * -> by = (3 << 1) - 1 = 5. Rows {1,0,1}, {0,1,0}, {0,0,0}. */
    const uint32_t T=ARENA+0xc00,P=ARENA+0xd00,ROWS=ARENA+0xd40,BYTES=ARENA+0xd80,CELLS=ARENA+0x1000;
    flat_reset();w32(0,FG+0xec,CELLS);w32(0,FG+0x100,3);w32(0,FG+0x104,3);w32(0,FG+0x108,14);w32(0,FG+0x121,T);
    for(uint32_t i=0;i<64;++i)w8(FM+i,0);
    w32(0,T+16+4,6);w32(0,P,3u*0x8000u+0x7fffu);w32(0,P+4,2u*0x8000u+5u);
    for(unsigned j=0;j<3;++j)w32(0,ROWS+4*j,BYTES+4*j);
    w8(BYTES+0,1);w8(BYTES+1,0);w8(BYTES+2,1);w8(BYTES+4,0);w8(BYTES+5,1);w8(BYTES+6,0);w8(BYTES+8,0);w8(BYTES+9,0);w8(BYTES+10,0);
    const uint32_t c65=CELLS+16*(5*8+6),c75=CELLS+16*(5*8+7),c64=CELLS+16*(4*8+6),c74=CELLS+16*(4*8+7);
    w8(c65+0xf,0x5b);w8(c75+0xf,0xfd);w8(c64+0xf,0x00);w8(c74+0xf,0x02);
    w16(0,VA(0x4d2680),0);
    uint32_t ecx=0;uint64_t p=es2_mark_raster_registers(m,&flat_graph,FG,P,2,ROWS,3,entry,&ecx);
    /* (byte & 2) | 2 << 2 */
    CHECK(r8(c65+0xf)==0x0a&&r8(c75+0xf)==0x08&&r8(c64+0xf)==0x08&&r8(c74+0xf)==0x0a);
    CHECK(flat_calls==3&&flat_log[0].args[1]==c65&&flat_log[1].args[1]==c75&&flat_log[2].args[1]==c65);
    CHECK(flat_log[0].args[2]==1&&regs_of(0,c65,(c65&0xffffff00u)|0x0a,8));
    CHECK(r8(FM+5*8+6)==1&&r8(FM+5*8+7)==1&&r8(FM+4*8+6)==0);
    /* the last cell (7,4), no call: EDX = its pointer with the new byte, ECX = k << 2 */
    CHECK(EAX(p)==3&&EDX(p)==((c74&0xffffff00u)|0x0a)&&ecx==8);
    /* option -l: the flag bytes stay; EDX = the cell, ECX = n + m */
    w8(c65+0xf,0x5b);w16(0,VA(0x4d2680),1);flat_calls=0;
    p=es2_mark_raster_registers(m,&flat_graph,FG,P,2,ROWS,3,entry,&ecx);
    CHECK(r8(c65+0xf)==0x5b&&flat_calls==3&&EAX(p)==3&&EDX(p)==c74&&ecx==6);
    /* count 0: EAX = rows, EDX = 0, ECX = 8 - t - s */
    flat_calls=0;p=es2_mark_raster_registers(m,&flat_graph,FG,P,2,ROWS,0,entry,&ecx);
    CHECK(flat_calls==0&&EAX(p)==ROWS&&EDX(p)==0&&ecx==1);
    /* a cell outside the grid (cy = by = 9, index 78 >= 64): the null cell
     * (with -l; without it the original would store at 0Fh) */
    w32(0,P+4,4u*0x8000u);w8(BYTES,0);flat_calls=0;   /* ((4 + 1) << 1) - 1 = 9 */
    p=es2_mark_raster_registers(m,&flat_graph,FG,P,2,ROWS,1,entry,&ecx);
    CHECK(flat_calls==0&&EAX(p)==1&&EDX(p)==0&&ecx==6);
}
int main(void){
    flag_get();flood_sum();flood_fill();flatten();raster();
    mark();cache();grid();cells();materials_dba();materials_text();zone();
    printf("terrain_load: %u checks passed\n",checks);
    return 0;
}
