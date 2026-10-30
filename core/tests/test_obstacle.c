#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/obstacle.h"
/* Synthetic state for the cores (obstacle_test 404ae4,
 * convoy_collide 46a614). The image base is placed so that the pool
 * pointer 4a95a0 and the actor list 4a9af8/4a9afe fall inside the arena.
 * The callees are small models that log their entry registers; 471c70
 * returns a planned sequence; the virtual slots are fixed addresses. All
 * values are synthetic. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE-0xa9000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define OBJ(i) (BASE+0x1000u+0x280u*(uint32_t)(i))
#define FRAME (BASE+0x21000u)
#define CFG (BASE+0x22000u)
#define REC (BASE+0x22100u)
#define VT (BASE+0x22200u)
#define LIST (BASE+0x22300u)
#define SLOT5C UINT32_C(0x12345678)
#define SLOT7C UINT32_C(0x23456789)
#define SLOT68 UINT32_C(0x3456789a)
static uint8_t arena[0x23000];static unsigned checks,ncalls,nseq,cursor;
static uint32_t calls[400],regs[400][3],args[400][4],seq[300],gate,step_edx,walk_eax;
static uint32_t list_copy[160];
static uint32_t dist_seq[8],cell_seq[8],d_copy[8][2],m_copy[8][4],r_copy[2];static unsigned ndist,ncell,nrot;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"obstacle line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(ncalls<400);calls[ncalls]=f;regs[ncalls][0]=r->eax;regs[ncalls][1]=r->edx;regs[ncalls][2]=r->ecx;
    for(unsigned j=0;j<n&&j<4;++j)args[ncalls][j]=a[j];
    ++ncalls;
    uint32_t eax=0x1000u+ncalls,edx=step_edx?step_edx:0x2000u+ncalls;
    if(f==VA(0x471c70)){CHECK(n==2&&a[0]==r32(0,VA(0x4a95a0)));eax=cursor<nseq?seq[cursor++]:0;}
    else if(f==VA(0x427c68)){CHECK(n==4);unsigned k=(uint16_t)a[3]<160?(uint16_t)a[3]:160;
        for(unsigned j=0;j<k;++j)list_copy[j]=r32(0,a[2]+4*j);eax=walk_eax;edx=a[0]+7;}
    else if(f==SLOT5C){CHECK(n==1);eax=0xabcd0000u|0x1234u;}
    else if(f==SLOT7C){CHECK(n==1);eax=0x55550000u|(uint16_t)(a[0]==OBJ(2)?0:0x300);}
    else if(f==VA(0x49288c)){CHECK(n==2);eax=gate;}
    else if(f==SLOT68){CHECK(n==1);eax=0x77770000u;edx=0x8888u;}
    else if(f==VA(0x404ae4)){CHECK(n==3);eax=walk_eax;}
    else if(f==VA(0x427da8)){CHECK(n==4);unsigned k=(uint16_t)a[3]<160?(uint16_t)a[3]:160;
        for(unsigned j=0;j<k;++j)list_copy[j]=r32(0,a[2]+4*j);eax=walk_eax;edx=a[0]+9;}
    /* Callees: distances and 427360 results from planned lists; the
     * matrix, swap and rotation models write what the original's do. */
    else if(f==VA(0x4928d0)){CHECK(n==2);eax=dist_seq[ndist++];}
    else if(f==VA(0x47ebb8)){CHECK(n==2);for(unsigned j=0;j<4;++j)w16(0,a[1]+2*j,(uint16_t)(0xa+j));}
    else if(f==VA(0x47df04)){CHECK(n==1);uint16_t t=r16(0,a[0]+2);w16(0,a[0]+2,r16(0,a[0]+4));w16(0,a[0]+4,t);eax=a[0];}
    else if(f==VA(0x47ffe8)){CHECK(n==3);for(unsigned j=0;j<2;++j)d_copy[nrot][j]=r32(0,a[0]+4*j);
        for(unsigned j=0;j<4;++j)m_copy[nrot][j]=r16(0,a[1]+2*j);
        w32(0,a[2],0x700u+nrot);w32(0,a[2]+4,0x800u+nrot);++nrot;}
    else if(f==VA(0x427360)){CHECK(n==3);r_copy[0]=r32(0,a[1]);r_copy[1]=r32(0,a[1]+4);eax=cell_seq[ncell++];}
    else if(f==VA(0x427238)){CHECK(n==3);r_copy[0]=r32(0,a[1]);r_copy[1]=r32(0,a[1]+4);eax=0x77;edx=a[0];}
    else CHECK(0);
    r->eax=eax;r->edx=edx;r->ecx=0x3000u+ncalls;
}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
/* A qualifying object: flag word +6 = 0, +4 != FFFFh. */
static void object(unsigned i,uint16_t flag,uint16_t kind,uint8_t dead,uint16_t level){
    uint32_t o=OBJ(i),c=CFG+0x20u*(i%6);
    w32(0,o,VT);w32(0,o+0x1f2,c);w32(0,o+0x205,REC);arena[o+0x99-BASE]=dead;
    w16(0,c+6,flag);w16(0,c+4,kind);w16(0,c+0x12,level);
}
int main(void){
    es2_weapon_graph g={IMAGE,FRAME,0,0,0,call,0,0,0};
    w32(0,VA(0x4a95a0),BASE+0x800);w16(0,REC+5,1);
    /* Empty pool: one 471c70 call (EAX = 0, EDX = list), then 427c68 with
     * count 0, EAX = the radius word, EDX = P, ECX = the list. */
    walk_eax=0;nseq=0;cursor=0;ncalls=0;step_edx=0x9999abcdu;
    uint64_t v=es2_obstacle_test_registers(&memory,&g,0x4444u,0xdead1388u,0,entry);
    CHECK(ncalls==2&&calls[0]==VA(0x471c70)&&regs[0][0]==0&&regs[0][1]==FRAME&&regs[0][2]==BASE+0x800);
    CHECK(args[0][1]==0&&calls[1]==VA(0x427c68)&&regs[1][0]==0x1388u&&regs[1][1]==0x4444u&&regs[1][2]==FRAME);
    CHECK(args[1][0]==0x4444u&&args[1][1]==0x1388u&&args[1][2]==FRAME&&args[1][3]==0x99990000u);
    CHECK(v==((uint64_t)(0x4444u+7)<<32));
    /* Filter: 0 excluded, 1 qualifies, 2 fails the first test (+4 = FFFFh),
     * 3 passes it (dead byte set), 4 is skipped by the second test, 5 is
     * a dead FFFFh object with level 2 (kept). */
    object(0,0,0,0,0);object(1,0,0,0,0);object(2,1,0xffff,0,0);object(3,1,0,1,0);object(4,0,0xffff,1,1);object(5,0,0xffff,1,2);
    for(unsigned i=0;i<6;++i)seq[i]=OBJ(i);
    nseq=6;cursor=0;ncalls=0;step_edx=0;walk_eax=0x00010000u;
    v=es2_obstacle_test_registers(&memory,&g,0x4444u,7,OBJ(0),entry);
    CHECK(ncalls==8&&calls[7]==VA(0x427c68)&&(uint16_t)args[7][3]==3);
    CHECK(list_copy[0]==OBJ(1)&&list_copy[1]==OBJ(3)&&list_copy[2]==OBJ(5));
    CHECK(args[1][1]==OBJ(0)&&regs[1][0]==OBJ(0));          /* excluded: registers untouched */
    CHECK(regs[2][1]==FRAME&&regs[2][0]==OBJ(1));            /* EDX = the old cursor */
    CHECK(regs[3][0]==0&&regs[3][1]==CFG+0x40u);             /* first test failed on FFFFh */
    CHECK(regs[4][0]==1&&regs[4][1]==FRAME+4);               /* passed, appended */
    CHECK(regs[5][0]==OBJ(4)+0x92u&&regs[5][1]==CFG+0x80u);        /* skipped by the second test */
    CHECK(r16(0,FRAME+0x236)==3&&r32(0,FRAME+0x230)==FRAME+12&&r32(0,FRAME+0x238)==0);
    CHECK((uint32_t)v==0x00010000u);
    /* Overflow: 150 qualifying objects. The 141st overwrites the cursor
     * slot; the rest land in object 140 from +4 on. */
    for(unsigned i=0;i<150;++i){object(i,0,0,0,0);seq[i]=OBJ(i);}
    memset(arena+(FRAME-BASE),0xcc,ES2_OBSTACLE_FRAME);
    nseq=150;cursor=0;ncalls=0;walk_eax=0;
    es2_obstacle_test_registers(&memory,&g,0x4444u,7,0,entry);
    CHECK(ncalls==152&&calls[151]==VA(0x427c68)&&(uint16_t)args[151][3]==150&&r16(0,FRAME+0x236)==150);
    CHECK(r32(0,FRAME+0x230)==OBJ(140)+40u&&r16(0,FRAME+0x234)==0xcccc&&r32(0,FRAME+0x238)==0);
    for(unsigned i=0;i<140;++i)CHECK(list_copy[i]==OBJ(i));
    CHECK(list_copy[140]==OBJ(140)+40u&&list_copy[141]==(150u<<16|0xccccu)&&list_copy[142]==0);
    for(unsigned k=141;k<150;++k)CHECK(r32(0,OBJ(140)+4u*(k-140))==OBJ(k));
    CHECK(r32(0,OBJ(140))==VT);
    /* convoy_collide: actor list of 4 (B itself, a pending one, one with
     * radius 0, then a hit), the radius 1234h + 300h bound. */
    uint32_t b=OBJ(10),grp=BASE+0x22400,grp2=BASE+0x22420;
    w32(0,VT+0x5c,SLOT5C);w32(0,VT+0x7c,SLOT7C);w32(0,VT+0x68,SLOT68);
    w32(0,VA(0x4a9af8),LIST);w16(0,VA(0x4a9afe),4);
    w32(0,LIST,b);w32(0,LIST+4,OBJ(1));w32(0,LIST+8,OBJ(2));w32(0,LIST+12,OBJ(3));w32(0,LIST+16,OBJ(4));
    w32(0,OBJ(1)+0x45,grp2);w32(0,grp2+0x14,1);w32(0,OBJ(2)+0x45,grp);w32(0,OBJ(3)+0x45,grp);w32(0,grp+0x14,0);
    gate=0x1533u;ncalls=0;
    v=es2_convoy_collide_registers(&memory,&g,b,entry);
    CHECK(ncalls==5&&calls[0]==SLOT5C&&regs[0][0]==LIST&&regs[0][1]==VT&&regs[0][2]==entry[2]);
    CHECK(calls[1]==SLOT7C&&args[1][0]==OBJ(2)&&regs[1][0]==0&&regs[1][2]==grp);
    CHECK(calls[2]==SLOT7C&&calls[3]==VA(0x49288c)&&args[3][0]==b+0x26&&args[3][1]==OBJ(3)+0x26);
    CHECK(regs[3][0]==OBJ(3)+0x26&&regs[3][1]==b+0x26&&regs[3][2]==OBJ(3));
    CHECK(calls[4]==SLOT68&&regs[4][0]==VT&&regs[4][1]==0x300u&&regs[4][2]==0x1534u);
    CHECK(v==((uint64_t)0x8888u<<32|0x77770001u));
    /* Bound reached: no hit, the walk ends at the count, then 404ae4 with
     * EDX = the last index word, EAX = 404ae4's high word | 1. */
    gate=0x1534u;ncalls=0;walk_eax=0x00420100u;
    v=es2_convoy_collide_registers(&memory,&g,b,entry);
    CHECK(ncalls==5&&calls[4]==VA(0x404ae4)&&args[4][0]==b+0x26&&(uint16_t)args[4][1]==0x1234u&&args[4][2]==b);
    CHECK((uint16_t)regs[4][1]==4&&(uint32_t)v==0x00420001u);
    /* A null entry before the count ends the walk too; AX = 0 stays. */
    w32(0,LIST+8,0);ncalls=0;walk_eax=0x00420000u;
    v=es2_convoy_collide_registers(&memory,&g,b,entry);
    CHECK(ncalls==2&&calls[1]==VA(0x404ae4)&&(uint16_t)regs[1][1]==2&&regs[1][2]==LIST+8&&(uint32_t)v==0x00420000u);
    /* pool_query 404bc0: empty pool, one step (EAX = EDX = 0, ECX = the
     * pool), no 427da8; EAX = 0, EDX = the step's. */
    uint32_t info=BASE+0x22500;
    nseq=0;cursor=0;ncalls=0;step_edx=0x7777abcdu;
    v=es2_pool_query_registers(&memory,&g,0x5555u,0x6666u,0xffffffffu,entry);
    CHECK(ncalls==1&&regs[0][0]==0&&regs[0][1]==0&&regs[0][2]==BASE+0x800&&args[0][1]==0);
    CHECK(v==((uint64_t)0x7777abcdu<<32));
    CHECK(r32(0,FRAME+0x230)==FRAME&&r32(0,FRAME+0x234)==0&&r16(0,FRAME+0x23a)==0&&r32(0,FRAME+0x23c)==0);
    /* Team filter 2: object 0 team 2 (kept), 1 team 3 (skipped: EAX = its
     * info, EDX = 3), 2 team byte 82h (EDX = FFFFFF82h, skipped), 3 fails
     * the first test (+4 = FFFFh), 4 passes it (dead byte set). */
    for(unsigned i=0;i<5;++i){object(i,0,0,0,0);seq[i]=OBJ(i);w32(0,OBJ(i)+0x45,info+0x20u*i);
        *(uint8_t *)host(0,info+0x20u*i+0x12)=0x02;}
    *(uint8_t *)host(0,info+0x20+0x12)=3;*(uint8_t *)host(0,info+0x40+0x12)=0x82;
    object(3,1,0xffff,0,0);object(4,1,0,1,0);
    nseq=5;cursor=0;ncalls=0;step_edx=0x99990000u;walk_eax=0x00020000u;
    v=es2_pool_query_registers(&memory,&g,0x5555u,0x6666u,0xabcd0002u,entry);
    CHECK(ncalls==7&&calls[6]==VA(0x427da8)&&args[6][0]==0x5555u&&args[6][1]==0x6666u&&args[6][2]==FRAME);
    CHECK(args[6][3]==0x99990000u+2u&&regs[6][0]==0x6666u&&regs[6][1]==0x5555u&&regs[6][2]==FRAME);
    CHECK(list_copy[0]==OBJ(0)&&list_copy[1]==OBJ(4));
    CHECK(regs[1][0]==info&&regs[1][1]==FRAME);             /* appended: EAX = info, EDX = old cursor */
    CHECK(regs[2][0]==info+0x20u&&regs[2][1]==3u);          /* team 3 skipped */
    CHECK(regs[3][0]==info+0x40u&&regs[3][1]==0xffffff82u); /* sign-extended team byte */
    CHECK(regs[4][0]==0&&regs[4][1]==CFG+0x60u);            /* first test failed */
    CHECK(regs[5][0]==1&&regs[5][1]==FRAME+4u);             /* passed it, appended */
    CHECK((uint32_t)v==0x00020000u&&(uint32_t)(v>>32)==0x5555u+9u&&r32(0,FRAME+0x234)==0);
    /* Filter word negative: no team read; a hit sets the result dword. */
    nseq=5;cursor=0;ncalls=0;walk_eax=0x00030100u;
    v=es2_pool_query_registers(&memory,&g,0x5555u,0x6666u,0x00008000u,entry);
    CHECK(ncalls==7&&(uint16_t)args[6][3]==4);
    CHECK(list_copy[0]==OBJ(0)&&list_copy[1]==OBJ(1)&&list_copy[2]==OBJ(2)&&list_copy[3]==OBJ(4));
    CHECK(regs[1][0]==OBJ(0)&&regs[2][0]==OBJ(1)&&regs[3][0]==OBJ(2)&&regs[4][0]==0&&regs[5][0]==1);
    CHECK((uint32_t)v==0x00030001u&&r32(0,FRAME+0x234)==1);
    /* Filter word 82h against team bytes 82h (FF82h after MOVSX): nothing
     * is listed, so no 427da8. */
    for(unsigned i=0;i<5;++i)*(uint8_t *)host(0,info+0x20u*i+0x12)=0x82;
    nseq=5;cursor=0;ncalls=0;step_edx=0x12345678u;
    v=es2_pool_query_registers(&memory,&g,0x5555u,0x6666u,0x00000082u,entry);
    CHECK(ncalls==6&&regs[5][0]==info+0x80u&&regs[5][1]==0xffffff82u&&v==((uint64_t)0x12345678u<<32));
    /* Overflow: 150 qualifying objects; the 141st overwrites the cursor
     * slot, the rest land in object 140 from +4 on; 427da8 then sees the
     * result dword, EBP-8 with the count and the iterator. */
    for(unsigned i=0;i<150;++i){object(i,0,0,0,0);seq[i]=OBJ(i);}
    memset(arena+(FRAME-BASE),0xcc,ES2_POOL_QUERY_FRAME);
    nseq=150;cursor=0;ncalls=0;walk_eax=0;
    es2_pool_query_registers(&memory,&g,0x5555u,0x6666u,0xffffu,entry);
    CHECK(ncalls==152&&calls[151]==VA(0x427da8)&&(uint16_t)args[151][3]==150&&r16(0,FRAME+0x23a)==150);
    CHECK(r32(0,FRAME+0x230)==OBJ(140)+40u&&r16(0,FRAME+0x238)==0xcccc&&r32(0,FRAME+0x23c)==0);
    for(unsigned i=0;i<140;++i)CHECK(list_copy[i]==OBJ(i));
    CHECK(list_copy[140]==OBJ(140)+40u&&list_copy[141]==0&&list_copy[142]==(150u<<16|0xccccu)&&list_copy[143]==0);
    for(unsigned k=141;k<150;++k)CHECK(r32(0,OBJ(140)+4u*(k-140))==OBJ(k));
    /* collision_walk 427c68: entry 0 at the shape radius (JGE
     * skips), entry 1 inside with the matrix flag clear (euler, flag 1,
     * 427360 = 0), entry 2 inside (negative distance) with the flag set
     * (427360 = 55h ends the walk). D/R/M live in the frame image. */
    const uint32_t mgr=BASE+0x22600,shape=BASE+0x22700,pt=BASE+0x22800,wl=BASE+0x22900,fr=FRAME+ES2_COLLISION_WALK_FRAME;
    w32(0,mgr+4,shape);w16(0,shape+8,0x100);
    for(unsigned i=0;i<3;++i){w32(0,OBJ(i)+0x34,mgr);w32(0,wl+4*i,OBJ(i));w32(0,OBJ(i)+0x26,0x1000u*i);w32(0,OBJ(i)+0x2a,0x10u*i);
        for(unsigned j=0;j<4;++j)w16(0,OBJ(i)+0x12+2*j,(uint16_t)(0x10*i+j));}
    w16(0,OBJ(1)+0x32,0);w16(0,OBJ(2)+0x32,1);w32(0,pt,0x5000);w32(0,pt+4,0x6000);
    dist_seq[0]=0x100;dist_seq[1]=0xff;dist_seq[2]=0x80000000u;cell_seq[0]=0;cell_seq[1]=0x55;
    ncalls=ndist=ncell=nrot=0;step_edx=0;
    v=es2_collision_walk_registers(&memory,&g,pt,0xdeadbeefu,wl,0x77770003u,entry);
    /* The swap 47df04 is computed in core (no call); m_copy shows
     * its effect on M. */
    CHECK(ncalls==8&&v==((uint64_t)(0x2000u+8)<<32|0x55u)&&r16(0,OBJ(1)+0x32)==1);
    CHECK(calls[0]==VA(0x4928d0)&&args[0][0]==pt&&args[0][1]==OBJ(0)+0x26&&regs[0][0]==OBJ(0)+0x26&&regs[0][1]==pt&&regs[0][2]==entry[2]);
    CHECK(calls[1]==VA(0x4928d0)&&regs[1][2]==0x100u&&regs[1][1]==pt);
    CHECK(calls[2]==VA(0x47ebb8)&&args[2][0]==OBJ(1)+0xc&&args[2][1]==OBJ(1)+0x12);
    CHECK(regs[2][0]==OBJ(1)+0x12&&regs[2][1]==OBJ(1)+0xc&&regs[2][2]==OBJ(1)+0xc);
    CHECK(calls[3]==VA(0x47ffe8)&&args[3][0]==fr-0x24&&args[3][1]==fr-0xc&&args[3][2]==fr-0x1c);
    CHECK(regs[3][0]==fr-0x1c&&regs[3][1]==fr-0xc&&regs[3][2]==fr-0x24);
    CHECK(d_copy[0][0]==0x5000u-0x1000u&&d_copy[0][1]==0x6000u-0x10u);
    CHECK(m_copy[0][0]==0xa&&m_copy[0][1]==0xc&&m_copy[0][2]==0xb&&m_copy[0][3]==0xd);
    CHECK(calls[4]==VA(0x427360)&&args[4][0]==shape&&args[4][1]==fr-0x1c&&args[4][2]==0&&regs[4][0]==fr-0x1c);
    CHECK(calls[5]==VA(0x4928d0)&&regs[5][2]==0x3005u&&calls[6]==VA(0x47ffe8));
    CHECK(m_copy[1][0]==0x20&&m_copy[1][1]==0x22&&m_copy[1][2]==0x21&&m_copy[1][3]==0x23);
    CHECK(d_copy[1][0]==0x5000u-0x2000u&&d_copy[1][1]==0x6000u-0x20u&&r_copy[0]==0x701u&&r_copy[1]==0x801u);
    /* No hit: the index word over the last callee's EDX high word. */
    w16(0,OBJ(1)+0x32,0);dist_seq[0]=0x100;dist_seq[1]=0xff;dist_seq[2]=0x7fffffffu;cell_seq[0]=0;
    ncalls=ndist=ncell=nrot=0;step_edx=0xabcd0000u;
    v=es2_collision_walk_registers(&memory,&g,pt,0,wl,3,entry);
    CHECK(ncalls==6&&v==((uint64_t)0xabcd0003u<<32));
    /* Count 0 and a negative count word: no call, EDX = the entry EDX. */
    ncalls=0;v=es2_collision_walk_registers(&memory,&g,pt,0,wl,0x12340000u,entry);
    CHECK(ncalls==0&&v==((uint64_t)entry[1]<<32));
    v=es2_collision_walk_registers(&memory,&g,pt,0,wl,0x8000u,entry);
    CHECK(ncalls==0&&v==((uint64_t)entry[1]<<32));
    /* shape_cell 427360: word +2Ah = 0 returns at once; else Q = R + the
     * origin words shifted by +32h (16-bit SHL, count masked to 5 bits),
     * cell with EDX = sx(x word) high word over the third slot's word. */
    const uint32_t sc=BASE+0x22a00,rp=BASE+0x22a80,qf=FRAME+ES2_SHAPE_CELL_FRAME;
    w32(0,rp,0x100);w32(0,rp+4,0x200);w16(0,sc+0x2a,0);
    ncalls=0;v=es2_shape_cell_registers(&memory,&g,sc,rp,0xabcd1234u,entry);
    CHECK(ncalls==0&&v==((uint64_t)entry[1]<<32));
    w16(0,sc+0x2a,4);w16(0,sc+0x2e,3);w16(0,sc+0x30,0xfffe);w16(0,sc+0x32,0x24);
    ncalls=0;v=es2_shape_cell_registers(&memory,&g,sc,rp,0xabcd1234u,entry);
    CHECK(ncalls==1&&calls[0]==VA(0x427238)&&args[0][0]==sc&&args[0][1]==qf-0xc&&args[0][2]==0x1234u);
    CHECK(regs[0][0]==sc&&regs[0][1]==0x1234u&&regs[0][2]==qf-0xc&&r_copy[0]==0x130u&&r_copy[1]==0x1e0u);
    CHECK(r16(0,qf-4)==0x30&&r16(0,qf-2)==0xffe0&&v==((uint64_t)sc<<32|0x77u));
    w16(0,sc+0x2e,0x8000);w16(0,sc+0x32,0x10);
    ncalls=0;v=es2_shape_cell_registers(&memory,&g,sc,rp,0x5678u,entry);
    CHECK(args[0][2]==0x5678u&&r_copy[0]==0x100u&&r_copy[1]==0x200u);
    w16(0,sc+0x32,0x20);
    ncalls=0;v=es2_shape_cell_registers(&memory,&g,sc,rp,0x5678u,entry);
    CHECK(args[0][2]==0xffff5678u&&r_copy[0]==0xffff8100u&&r_copy[1]==0x200u-2u);
    printf("obstacle_test: %u checks passed\n",checks);
    return 0;
}
