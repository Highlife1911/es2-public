#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/results_writer.h"
/* Synthetic guest: objects at 90000000h, scratch at A0000000h, image base
 * B with the globals page G(4a9000h). Every game call is logged with its
 * arguments and entry registers and returns planned registers; callbacks
 * emulate what the callees write (stream objects, the record's saved head,
 * the bytes the writes receive). */
#define OBJ UINT32_C(0x90000000)
#define FRAME UINT32_C(0xa0000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
#define FN_WRITE UINT32_C(0x77000014)
static uint8_t arena[8192],scratch[0x60],page[8192];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"results_writer line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-FRAME<sizeof(scratch))return scratch+(a-FRAME);
    if(a-G(0x4a9000)<sizeof(page))return page+(a-G(0x4a9000));
    fprintf(stderr,"results_writer: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static void w8(uint32_t a,uint8_t v){*(uint8_t *)host(0,a)=v;}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
typedef struct logged { uint32_t f,a[6],r[3],data; unsigned n,observed; } logged;
static logged log_[64];static unsigned calls,observed;
static uint32_t ret[64][3];
/* Byte streams the emulated writes produce (results.dat, mission.var). */
static uint8_t results[512],bank[2000];static size_t results_size,bank_size;
static uint32_t stream_error[2];static unsigned opens,members;
static void (*hook)(unsigned call);
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<64);logged *l=log_+calls;l->f=f;l->n=n;if(n)memcpy(l->a,a,4*n);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;l->observed=observed;
    if(f==G(0x426ac8))l->data=r32(0,a[0]);
    if(f==G(0x49630c)){CHECK(n==0);w32(0,FRAME+0x58-0x38,0x0badf00du);w16(0,FRAME+0x58-0x28,0);}
    if(f==G(0x47b064)){w32(0,a[0],OBJ+0x1f00);w32(0,a[0]+4,stream_error[opens++&1]);}
    if(f==FN_WRITE){CHECK(n==3);l->data=r16(0,a[2]);for(uint32_t i=0;i<a[1];++i)results[results_size++]=*(uint8_t *)host(0,a[2]+i);}
    if(f==G(0x423d68)) /* result_conditions: 72 bytes per member through the stream */
        for(unsigned k=0;k<members;++k)for(unsigned i=0;i<72;++i)results[results_size++]=(uint8_t)(k*72+i);
    if(f==G(0x47b2b8)){for(uint32_t i=0;i<a[1];++i)bank[i]=*(uint8_t *)host(0,a[2]+i);bank_size=a[1];}
    if(hook)hook(calls);
    r->eax=ret[calls][0];r->edx=ret[calls][1];r->ecx=ret[calls][2];++calls;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;(void)slot;++observed;}
static es2_weapon_graph graph(void){es2_weapon_graph g={B,FRAME,0,0,0,call,0,observe,0};return g;}
static const uint32_t entry[3]={0x11112222u,0x33334444u,0x55556666u};
static void reset(void){
    calls=observed=0;opens=members=0;hook=NULL;results_size=bank_size=0;memset(ret,0,sizeof(ret));memset(log_,0,sizeof(log_));
    memset(arena,0,sizeof(arena));memset(scratch,0,sizeof(scratch));memset(page,0,sizeof(page));
    memset(results,0,sizeof(results));stream_error[0]=stream_error[1]=0;
}
static int16_t s16(const uint8_t *p){return (int16_t)(p[0]|p[1]<<8);}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

static void test_list_append(void){
    const uint32_t list=OBJ+0x100,item=OBJ+0x10;uint32_t ecx=0;
    reset();w16(0,G(0x4a9e6a),2);w32(0,G(0x4a9e6c),list);w32(0,item,0x12345678u);
    uint64_t r=es2_list_append_registers(&memory,B,item,&ecx);
    CHECK((uint32_t)r==2&&r>>32==list&&ecx==0x12345678u&&r32(0,list+8)==0x12345678u&&r16(0,G(0x4a9e6a))==3);
    /* Negative count: below the list, the count wraps to 0. */
    w16(0,G(0x4a9e6a),0xffff);w32(0,item,0xcafebabeu);
    r=es2_list_append_registers(&memory,B,item,&ecx);
    CHECK((uint32_t)r==0xffffffffu&&r32(0,list-4)==0xcafebabeu&&r16(0,G(0x4a9e6a))==0);
    /* The store hits the count word: the increment sees the stored word. */
    w16(0,G(0x4a9e6a),0);w32(0,G(0x4a9e6c),G(0x4a9e6a));w32(0,item,0xaaaa0005u);
    r=es2_list_append_registers(&memory,B,item,&ecx);
    CHECK((uint32_t)r==0&&r>>32==G(0x4a9e6a)&&r16(0,G(0x4a9e6a))==6&&r16(0,G(0x4a9e6c))==0xaaaa);
    CHECK(es2_list_append_registers(&memory,B,item,NULL)>>32==((G(0x4a9e6a)&0xffff0000u)|0xaaaau));
}

/* Vehicle V at OBJ+0x400: weapon list W, items I1 (slot -1), I2 (slot 3),
 * configuration C. */
#define V (OBJ+0x400)
#define W (OBJ+0x800)
#define ITEMS (OBJ+0x840)
#define I1 (OBJ+0x880)
#define I2 (OBJ+0x8c0)
#define D1 (OBJ+0x900)
#define D2 (OBJ+0x940)
#define C (OBJ+0x980)
static void vehicle(void){
    w32(0,V+0x202,W);w16(0,W+8,3);w32(0,W+4,ITEMS);
    w32(0,ITEMS,I1);w32(0,ITEMS+4,0);w32(0,ITEMS+8,I2);
    w32(0,I1+0x18,D1);w8(D1+0x17,0xff);w32(0,I1+0x1c,D1+0x10);w16(0,D1+0x10+0x56,0x0123);
    w32(0,I2+0x18,D2);w8(D2+0x17,3);w32(0,I2+0x1c,D2+0x10);w16(0,D2+0x10+0x56,0x0456);
    w32(0,V+0x1f2,C);w16(0,C+0x54,0x8003);
}
static void shorten(unsigned call){if(call==0)w16(0,W+8,1);}
static void test_vehicle_value(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    /* Excluded vehicle: no call, EAX 0, entry EDX, ECX the scratch f-4. */
    reset();vehicle();w8(V+0xb3,1);w32(0,FRAME+4,0x5a5a5a5au);
    uint64_t r=es2_vehicle_value_registers(&memory,&g,V,entry,&ecx);
    CHECK(calls==0&&r==(uint64_t)entry[1]<<32&&ecx==0x5a5a5a5au);
    /* I1: ratio 40h (appended), the null item skipped, I2: ratio 80h
     * (skipped); the final ratio 100h halves the base word. */
    reset();vehicle();
    ret[0][0]=0x77770040u;ret[0][1]=0x88880000u;ret[0][2]=0x9999u;
    ret[1][0]=0xabcdu;ret[1][1]=0xbcdeu;ret[1][2]=0xcdefu;
    ret[2][0]=0x80u;ret[2][1]=0x1u;ret[2][2]=0x2u;
    ret[3][0]=0x12340100u;ret[3][1]=0x3u;ret[3][2]=0x4u;
    ret[4][0]=5000;ret[4][1]=0x5u;ret[4][2]=0x6u;
    ret[5][0]=0x4242u;ret[5][1]=0x4343u;ret[5][2]=0x4444u;
    r=es2_vehicle_value_registers(&memory,&g,V,entry,&ecx);
    CHECK(calls==6&&observed==6&&(uint32_t)r==0x4242u&&r>>32==0x4343u);
    CHECK(log_[0].f==G(0x40dbc0)&&log_[0].n==2&&log_[0].a[0]==V+0x206&&log_[0].a[1]==0xffff0012u);
    CHECK(log_[0].r[0]==0xffff0012u&&log_[0].r[1]==V+0x206&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x426ac8)&&log_[1].n==1&&log_[1].a[0]==FRAME+4&&log_[1].data==(0x4bu<<16|0x0123u));
    CHECK(log_[1].r[0]==0x4bu&&log_[1].r[1]==FRAME+4&&log_[1].r[2]==0xc0u);
    CHECK(log_[2].f==G(0x40dbc0)&&log_[2].a[1]==0x16u&&log_[2].r[0]==0x16u&&log_[2].r[2]==FRAME+4);
    CHECK(log_[3].f==G(0x40dbc0)&&log_[3].a[0]==V+0x206&&log_[3].a[1]==0);
    CHECK(log_[3].r[0]==V+0x206&&log_[3].r[1]==W&&log_[3].r[2]==C);
    CHECK(log_[4].f==G(0x41537c)&&log_[4].n==1&&log_[4].a[0]==V+0x206&&log_[4].r[0]==0x12340100u&&log_[4].r[2]==0x4u);
    CHECK(log_[5].f==G(0x47e0b0)&&log_[5].n==2&&log_[5].a[0]==5000&&log_[5].a[1]==0xffffc001u);
    CHECK(log_[5].r[0]==0xffffc001u&&log_[5].r[1]==0xffffc001u&&log_[5].r[2]==V+0x206);
    CHECK(ecx==(0x4bu<<16|0x0123u)&&r32(0,FRAME)==0xffffc001u);
    /* EDX at the append keeps part_ratio's high word in the id word. */
    CHECK(r16(0,FRAME+4)==0x0123);
    /* Ratio 7Fh appends 50 percent; a final ratio 1100h does not halve;
     * part_ratio shortens the list (count reread). */
    reset();vehicle();hook=shorten;ret[0][0]=0x7fu;ret[2][0]=0x1100u;
    r=es2_vehicle_value_registers(&memory,&g,V,entry,&ecx);
    CHECK(calls==5&&log_[1].data==(50u<<16|0x0123u)&&log_[2].r[2]==C&&log_[4].a[1]==0xffff8003u);
    /* A negative ratio: percentage above 100. */
    reset();vehicle();w16(0,W+8,1);ret[0][0]=0xff00u;
    r=es2_vehicle_value_registers(&memory,&g,V,entry,&ecx);
    CHECK(log_[1].data==(200u<<16|0x0123u)&&log_[1].r[2]==0x200u);
}

/* MECH list cursors c1..c4 at OBJ+0x1000+k*0x100; team sides at T1/T2. */
#define TEAM (OBJ+0x1800)
#define SIDE_A (OBJ+0x1840)
#define SIDE_B (OBJ+0x1880)
#define CUR(k) (OBJ+0x1000+(k)*0x100u)
static void relist(unsigned call){if(call==0)w32(0,G(0x4a9b7a),0x13572468u);}
static void test_score_sum(void){
    es2_weapon_graph g=graph();uint32_t ecx=1;
    reset();w32(0,G(0x4a9b7a),OBJ+0x1f00);w8(TEAM+0x12,0xfe);w8(SIDE_A+0x12,0xfe);w8(SIDE_B+0x12,1);
    for(unsigned k=1;k<=4;++k)w32(0,CUR(k)+0x45,k==1?SIDE_A:SIDE_B);
    w8(CUR(2)+0xa4,1);w8(CUR(3)+0x99,1);
    hook=relist;
    /* pool_prev c1 (same side), c2 (counted), vehicle_value, c3 (second
     * flag), vehicle_value, c4 (not counted), 0. */
    ret[0][0]=CUR(1);ret[0][1]=0xe0;ret[0][2]=0xc0;
    ret[1][0]=CUR(2);ret[1][1]=0xe1;ret[1][2]=0xc1;
    ret[2][0]=100;ret[2][1]=0xe2;ret[2][2]=0xc2;
    ret[3][0]=CUR(3);ret[3][1]=0xe3;
    ret[4][0]=0xfffffff0u;ret[4][1]=0xe4;
    ret[5][0]=CUR(4);ret[5][1]=0xe5;
    ret[6][0]=0;ret[6][1]=0xe6;ret[6][2]=0xc6;
    uint64_t r=es2_score_sum_registers(&memory,&g,TEAM,entry,&ecx);
    CHECK(calls==7&&observed==7&&(uint32_t)r==84&&r>>32==0xe6&&ecx==0);
    CHECK(log_[0].f==G(0x471c70)&&log_[0].n==2&&log_[0].a[0]==OBJ+0x1f00&&log_[0].a[1]==0);
    CHECK(log_[0].r[0]==0&&log_[0].r[1]==OBJ+0x1f00&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].a[0]==0x13572468u&&log_[1].a[1]==CUR(1)&&log_[1].r[0]==0xfffffffeu&&log_[1].r[1]==0x13572468u&&log_[1].r[2]==0xfffffffeu);
    CHECK(log_[2].f==G(0x418e60)&&log_[2].n==1&&log_[2].a[0]==CUR(2)&&log_[2].r[0]==1&&log_[2].r[1]==1&&log_[2].r[2]==0xfffffffeu);
    CHECK(log_[3].a[1]==CUR(2)&&log_[3].r[0]==100&&log_[3].r[2]==CUR(2));
    CHECK(log_[4].a[0]==CUR(3)&&log_[4].r[0]==1&&log_[4].r[1]==CUR(3)+0x92);
    CHECK(log_[5].r[0]==0xfffffff0u&&log_[5].r[2]==CUR(3));
    CHECK(log_[6].a[1]==CUR(4)&&log_[6].r[0]==0&&log_[6].r[2]==0xfffffffeu);
    /* Empty list. */
    reset();ret[0][1]=0x77;
    r=es2_score_sum_registers(&memory,&g,TEAM,entry,NULL);
    CHECK(calls==1&&r==(uint64_t)0x77<<32);
}

/* The writer: P with team T, salvage list L of two pairs. */
#define P (OBJ+0x100)
#define T (OBJ+0x200)
#define L (OBJ+0x300)
#define TABLE (OBJ+0x1f00)
static const uint32_t f=FRAME+0x58;
static void writer_state(void){
    /* The bank first: 4a9e98 is its word 14h. */
    for(unsigned i=0;i<2000;++i)page[0xe70+i]=(uint8_t)(i*7);
    w32(0,P+0x45,T);w32(0,TABLE+0x14,FN_WRITE);
    w16(0,G(0x4a9e54),0x0007);w16(0,G(0x4a9e98),0xfffe);w16(0,G(0x4a9e6a),2);w32(0,G(0x4a9e6c),L);
    w16(0,L,0x0011);w16(0,L+2,0x0022);w16(0,L+4,0x0033);w16(0,L+6,0x0044);
}
static unsigned find(unsigned from,uint32_t fn){for(unsigned i=from;i<calls;++i)if(log_[i].f==fn)return i;return 99;}
/* The second pair write lowers the count to 1 (reread at the test) and the
 * list pointer changes (read once). */
static void count_hook(unsigned call){
    if(log_[call].f==FN_WRITE&&log_[call].a[2]==f-0x48){w16(0,G(0x4a9e6a),1);w32(0,G(0x4a9e6c),OBJ+0x1234);}
}
static void test_results_writer(void){
    es2_weapon_graph g=graph();uint32_t ecx=0;
    reset();writer_state();members=2;
    /* 0 49630c, 1 413280 (status 9), 2 45ee00, 3 47b064, 4 flag write,
     * 5 423e88, 6 47e0b0, 7 tally, 8 count, 9..12 pairs, 13 423d68, 14
     * close, 15 45ee00, 16 47b064, 17 47b2b8, 18 close. */
    ret[0][0]=0xfeedface;ret[0][1]=0x1;ret[0][2]=0x2;
    ret[1][0]=0xabcd0009u;ret[1][1]=0x3;ret[1][2]=0x4;
    ret[2][0]=G(0x4d285c);ret[2][1]=0x5;ret[2][2]=0x6;
    ret[3][0]=0x7;ret[3][1]=0x8;ret[3][2]=0x9;
    ret[4][0]=0x1a;ret[4][1]=0xdddd0000u;ret[4][2]=0x1c;
    ret[5][0]=1000;ret[5][1]=0x2b;ret[5][2]=0x2c;   /* score_sum */
    ret[6][0]=2441;ret[6][1]=0x3b;ret[6][2]=0x3c;   /* multiply_q10 */
    ret[7][0]=0x4a;ret[7][1]=0xeeee0000u;ret[7][2]=0x4c;
    ret[8][0]=0x5a5a0000u;ret[8][1]=0x5b;ret[8][2]=0x5c;
    for(unsigned i=9;i<64;++i){ret[i][0]=0x100u*i;ret[i][1]=0x100u*i+1;ret[i][2]=0x100u*i+2;}
    ret[9][0]=0x12340900u;
    uint64_t r=es2_results_writer_registers(&memory,&g,P,entry,&ecx);
    CHECK(calls==19&&observed==19);
    CHECK(log_[0].f==G(0x49630c)&&log_[0].r[0]==G(0x49a76c)&&log_[0].r[1]==entry[1]&&log_[0].r[2]==entry[2]);
    CHECK(log_[1].f==G(0x413280)&&log_[1].n==1&&log_[1].a[0]==P&&log_[1].r[0]==0xfeedface&&log_[1].r[1]==T&&log_[1].r[2]==0x2);
    CHECK(log_[2].f==G(0x45ee00)&&log_[2].a[0]==G(0x49a97d)&&log_[2].r[0]==0xabcd0009u&&log_[2].r[1]==3&&log_[2].r[2]==1);
    CHECK(log_[3].f==G(0x47b064)&&log_[3].n==2&&log_[3].a[0]==f-0xc&&log_[3].a[1]==G(0x4d285c));
    CHECK(log_[3].r[0]==G(0x4d285c)&&log_[3].r[1]==5&&log_[3].r[2]==G(0x49a97d));
    /* flag write: EAX = the table, EDX = 47b064's. */
    CHECK(log_[4].f==FN_WRITE&&log_[4].a[0]==f-0xc&&log_[4].a[1]==2&&log_[4].a[2]==f-0x3e&&log_[4].data==1);
    CHECK(log_[4].r[0]==TABLE&&log_[4].r[1]==8&&log_[4].r[2]==f-0x3e);
    CHECK(log_[5].f==G(0x423e88)&&log_[5].n==2&&log_[5].a[0]==T&&log_[5].a[1]==0xdddd0007u);
    CHECK(log_[5].r[0]==0x1a&&log_[5].r[1]==0xdddd0007u&&log_[5].r[2]==T);
    CHECK(log_[6].f==G(0x47e0b0)&&log_[6].a[0]==0x9c4&&log_[6].a[1]==1000&&log_[6].r[0]==1000&&log_[6].r[1]==0x2b&&log_[6].r[2]==0x2c);
    CHECK(log_[7].f==FN_WRITE&&log_[7].a[1]==4&&log_[7].a[2]==f-0x44&&r32(0,f-0x44)==2441u-50000u);
    CHECK(log_[7].r[0]==TABLE&&log_[7].r[1]==(uint32_t)-50000&&log_[7].r[2]==f-0x44);
    CHECK(log_[8].a[2]==f-0x46&&log_[8].data==2&&log_[8].r[1]==0xeeee0002u&&log_[8].r[2]==f-0x46);
    CHECK(log_[9].a[2]==f-0x48&&log_[9].data==0x11&&log_[9].r[0]==0x5a5a0011u&&log_[9].r[1]==f-0x48&&log_[9].r[2]==TABLE);
    CHECK(log_[10].a[2]==f-0x4a&&log_[10].data==0x22&&log_[10].r[0]==0x12340022u&&log_[10].r[1]==f-0x4a);
    CHECK(log_[12].a[2]==f-0x4a&&log_[12].data==0x44);
    CHECK(log_[13].f==G(0x423d68)&&log_[13].a[0]==T&&log_[13].a[1]==f-0xc&&log_[13].r[0]==T&&log_[13].r[1]==0xc01&&log_[13].r[2]==0xc02);
    CHECK(log_[14].f==G(0x47b09c)&&log_[14].a[0]==f-0xc&&log_[14].a[1]==2&&log_[14].r[0]==0xd00);
    CHECK(log_[15].f==G(0x45ee00)&&log_[15].a[0]==G(0x49a9a4)&&log_[15].r[0]==0xe00);
    CHECK(log_[16].f==G(0x47b064)&&log_[16].a[0]==f-0x18&&log_[16].a[1]==0xf00&&log_[16].r[0]==0xf00&&log_[16].r[1]==f-0x18&&log_[16].r[2]==G(0x49a9a4));
    CHECK(log_[17].f==G(0x47b2b8)&&log_[17].n==3&&log_[17].a[0]==f-0x18&&log_[17].a[1]==0x7d0&&log_[17].a[2]==G(0x4a9e70));
    CHECK(log_[17].r[0]==0&&log_[17].r[1]==0x1001&&log_[17].r[2]==f-0x18);
    CHECK(log_[18].f==G(0x47b09c)&&log_[18].a[0]==f-0x18&&log_[18].r[0]==f-0x18&&log_[18].r[1]==0x1101&&log_[18].r[2]==0x1102);
    CHECK(find(0,G(0x479c78))==99&&r16(0,f-0x28)==0x14);
    CHECK((uint32_t)r==0x1200&&r>>32==0x0badf00du&&ecx==0x1202);
    /* The bytes, read back with the shell's results grammar (campaign.c
     * run(): outcome word, u32 tally, i16 pairs, four bytes per pair,
     * then the 72-byte actor records). */
    CHECK(results_size==2+4+2+4*2+72*2);
    CHECK(s16(results)==1&&u32(results+2)==2441u-50000u&&s16(results+6)==2);
    CHECK(s16(results+8)==0x11&&s16(results+10)==0x22&&s16(results+12)==0x33&&s16(results+14)==0x44);
    CHECK(results[16]==0&&results[16+72+71]==(uint8_t)(143));
    CHECK(bank_size==2000&&bank[1]==7&&bank[0x28]==0xfe&&bank[0x29]==0xff&&bank[1999]==(uint8_t)(1999*7));
    /* Status other than 9; both streams fail (reports with their lines);
     * a pair write lowers the count and moves the list. */
    reset();writer_state();stream_error[0]=2;stream_error[1]=3;hook=count_hook;
    ret[1][0]=0x00090008u;
    for(unsigned i=2;i<64;++i){ret[i][0]=0x100u*i;ret[i][1]=0x100u*i+1;ret[i][2]=0x100u*i+2;}
    r=es2_results_writer_registers(&memory,&g,P,entry,&ecx);
    unsigned k=find(0,G(0x479c78));
    CHECK(k==4&&log_[k].n==6&&log_[k].a[0]==G(0x6c56c8)&&log_[k].a[1]==5&&log_[k].a[2]==0&&log_[k].a[3]==G(0x49a98e));
    CHECK(log_[k].a[4]==0x6f&&log_[k].a[5]==G(0x49a998)&&log_[k].r[0]==2&&log_[k].r[1]==0&&log_[k].r[2]==0x302);
    CHECK(log_[5].f==FN_WRITE&&log_[5].data==0&&log_[5].r[1]==0x401);
    unsigned w=find(k+1,G(0x423d68));
    CHECK(w==12&&log_[11].a[2]==f-0x4a&&log_[11].data==0x22);
    k=find(w,G(0x479c78));
    CHECK(k==16&&log_[k].a[3]==G(0x49a9b5)&&log_[k].a[4]==0x84&&log_[k].a[5]==G(0x49a9bf)&&log_[k].r[0]==3&&log_[k].r[1]==0);
    CHECK(log_[17].f==G(0x47b2b8)&&log_[17].r[0]==0x1000&&log_[17].r[1]==0x1001);
    CHECK(results_size==2+4+2+4&&s16(results)==0&&s16(results+6)==2);
}

int main(void){
    test_list_append();test_vehicle_value();test_score_sum();test_results_writer();
    printf("results_writer: %u checks passed\n",checks);
    return 0;
}
