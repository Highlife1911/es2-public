#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/world.h"
/* Synthetic guest: objects at 90000000h and an image base B with the
 * globals the terrain draw helpers read (texture table pointer, eye
 * position, terrain grid pointer). Expected values are worked out by hand
 * from the contracts in docs/reverse-engineering/terrain.md. */
#define OBJ UINT32_C(0x90000000)
#define B UINT32_C(0x10000000)
#define G(va) (B+(va)-0x400000u)
static uint8_t arena[4096],eye[0x10],grid_pointer[4];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"world line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-OBJ<sizeof(arena))return arena+(a-OBJ);
    if(a-G(0x6b4f40)<sizeof(eye))return eye+(a-G(0x6b4f40));
    if(a-G(0x4a0be0)<sizeof(grid_pointer))return grid_pointer+(a-G(0x4a0be0));
    fprintf(stderr,"world: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host,0};
static uint64_t pair(uint32_t eax,uint32_t edx){return (uint64_t)edx<<32|eax;}
static void polygon(uint32_t at,uint32_t points,const int32_t *xy,unsigned count){
    w32(0,at,count);w32(0,at+4,points);for(unsigned i=0;i<2*count;++i)w32(0,points+4*i,(uint32_t)xy[i]);
}
static int points_are(uint32_t at,const int32_t *xy,unsigned count){
    if(r32(0,at)!=count)return 0;
    uint32_t p=r32(0,at+4);for(unsigned i=0;i<2*count;++i)if(r32(0,p+4*i)!=(uint32_t)xy[i])return 0;
    return 1;
}
static void clip_checks(void){
    static const int32_t square[]={0,0,10,0,10,10,0,10};
    uint32_t in=OBJ,out=OBJ+0x10,ip=OBJ+0x100,op=OBJ+0x200;
    /* Keep x >= 5: enter at (5,0), leave at (5,10). */
    memset(arena,0,sizeof(arena));polygon(in,ip,square,4);w32(0,out+4,op);
    uint64_t r=es2_poly_clip_registers(&memory,1,1,5,in,out);
    static const int32_t right[]={5,0,10,0,10,10,5,10};CHECK(points_are(out,right,4)&&r==pair(4,out));
    /* Keep y <= 4 (keep byte zero, axis byte zero; high bytes ignored). */
    r=es2_poly_clip_registers(&memory,0x100,0x200,4,in,out);
    static const int32_t low[]={0,4,0,0,10,0,10,4};CHECK(points_are(out,low,4)&&r==pair(4,out));
    /* Quotients truncate toward zero: -14/-5 = 2 and -21/5 = -4; EAX keeps
     * the high 24 bits of the last y (-7). */
    static const int32_t edge[]={-3,0,2,-7};polygon(in,ip,edge,2);
    r=es2_poly_clip_registers(&memory,1,1,0,in,out);
    static const int32_t cut[]={0,-5,0,-4,2,-7};CHECK(points_are(out,cut,3)&&r==pair(0xffffff03u,out));
    /* Everything outside: nothing is emitted. */
    r=es2_poly_clip_registers(&memory,1,1,100,in,out);CHECK(r32(0,out)==0&&r==pair(0xffffff00u,out));
    /* Empty input. */
    w32(0,in,0x80000000u);w32(0,out,7);r=es2_poly_clip_registers(&memory,1,1,0,in,out);CHECK(r32(0,out)==0&&r==0);
}
typedef struct logged { uint32_t f,a[3],r[3]; unsigned n; } logged;
static logged log_[4];static unsigned calls,observed;static int converter_writes;
static void call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(calls<4&&n==3&&f==G(0x4930c1));logged *l=log_+calls++;l->f=f;l->n=n;memcpy(l->a,a,12);
    l->r[0]=r->eax;l->r[1]=r->edx;l->r[2]=r->ecx;
    if(converter_writes){static const uint32_t rows[]={0,2,0,4,1,3};for(unsigned i=0;i<6;++i)w32(0,a[2]+4*i,rows[i]);}
    r->eax=0x11;r->edx=0x22;r->ecx=0x33;
}
static void observe(void *c,uint32_t f,unsigned slot){(void)c;CHECK(f==0x4930c1&&!slot);++observed;}
static void spans_checks(void){
    es2_weapon_graph g={B,0,0,0,0,call,0,observe,0};
    uint32_t poly=OBJ,pts=OBJ+0x100,spans=OBJ+0x200,extent=OBJ+0x300;
    static const int32_t tri[]={5,7,9,3,6,12};
    memset(arena,0,sizeof(arena));polygon(poly,pts,tri,3);calls=observed=0;converter_writes=1;
    uint64_t r=es2_poly_spans_registers(&memory,&g,poly,spans,extent);
    static const int32_t moved[]={0,4,4,0,1,9};CHECK(points_are(poly,moved,3));
    CHECK(calls==1&&observed==1&&log_[0].a[0]==3&&log_[0].a[1]==pts&&log_[0].a[2]==spans);
    CHECK(log_[0].r[0]==pts&&log_[0].r[1]==3&&log_[0].r[2]==3);
    CHECK(r32(0,extent)==3&&r32(0,extent+4)==4&&r32(0,extent+8)==2&&r32(0,extent+12)==spans+8);
    CHECK(r32(0,spans+8)==5&&r32(0,spans+12)==9&&r32(0,spans+16)==6&&r32(0,spans+20)==8);
    CHECK(r==pair(spans+24,0));
    /* No points: ECX is the first x, the converter leaves the buffer, a
     * negative row count skips the loop and EDX keeps the doubled count. */
    w32(0,poly,0);w32(0,pts,0xabcdu);w32(0,pts+4,0x1234u);w32(0,spans,10);w32(0,spans+4,0xffffffffu);
    calls=0;converter_writes=0;r=es2_poly_spans_registers(&memory,&g,poly,spans,extent);
    CHECK(calls==1&&log_[0].r[2]==0xabcdu&&log_[0].a[0]==0&&r32(0,pts)==0xabcdu);
    CHECK(r32(0,extent)==10+0x1234u&&r32(0,extent+4)==0xffffffffu+10+0x1234u-1u&&r32(0,extent+8)==0xffffffffu);
    CHECK(r==pair(spans+8,0xfffffffeu));
}
static void rect_checks(void){
    uint32_t map=OBJ,poly=OBJ+0x200,pts=OBJ+0x300;
    memset(arena,0,sizeof(arena));
    w32(0,map+0x108,0x24);w32(0,map+0x10c,2);w32(0,map+0xf4,100);w32(0,map+0xf8,200);w32(0,map+0xfc,0x77);
    w32(0,map+0x100,3);w32(0,map+0x104,2);w32(0,poly+4,pts);
    w32(0,G(0x6b4f44),150);w32(0,G(0x6b4f48),210);
    uint64_t r=es2_view_rect_registers(&memory,B,map,poly);
    static const uint32_t want[]={118,200,0x77,118,232,0x77,182,232,0x77,182,200,0x77};
    CHECK(r==pair(poly,0x77)&&r32(0,poly)==4);for(unsigned i=0;i<12;++i)CHECK(r32(0,pts+4*i)==want[i]);
    /* Eye beyond the right edge: empty. */
    w32(0,G(0x6b4f44),1000);r=es2_view_rect_registers(&memory,B,map,poly);CHECK(r==pair(poly,0)&&r32(0,poly)==0);
    /* z is read after the point writes: points overlapping the map's height
     * field see the stored y1 of the second point (pts + 0x10 = map + 0xfc). */
    w32(0,G(0x6b4f44),150);w32(0,poly+4,map+0xec);r=es2_view_rect_registers(&memory,B,map,poly);CHECK(r==pair(poly,232));
}
static void uv_checks(void){
    uint32_t table=OBJ+0x100,cell=OBJ,out=OBJ+0x300,map=OBJ+0x400;
    memset(arena,0,sizeof(arena));w32(0,G(0x6b4f40),table);w32(0,G(0x4a0be0),map);w32(0,map+0x108,12);
    for(unsigned i=0;i<4;++i)w32(0,table+40+4*i,i+1);w16(0,table+40+16,0x8001);
    w32(0,cell,2);w32(0,cell+4,0);
    uint64_t r=es2_cell_uv_registers(&memory,B,cell,0x7f,0x41,out);
    static const uint32_t stored[]={1,2,1,4,3,4,3,2};
    CHECK(r==pair(0xffff8001u,table+40));for(unsigned i=0;i<8;++i)CHECK(r32(0,out+4*i)==stored[i]);
    /* Level 3 on a 2^12 grid cell: k = 2, size 4. */
    w32(0,cell+4,3);r=es2_cell_uv_registers(&memory,B,cell,0x7f,0x41,out);
    static const uint32_t scaled[]={0xfc,0xfffffffcu,0xfc,0xfffffff8u,0x100,0xfffffff8u,0x100,0xfffffffcu};
    CHECK(r==pair(0xffff8001u,table+40));for(unsigned i=0;i<8;++i)CHECK(r32(0,out+4*i)==scaled[i]);
}
int main(void){
    clip_checks();spans_checks();rect_checks();uv_checks();
    printf("PASS: world %u checks\n",checks);return 0;
}
